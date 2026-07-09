# qa_ra_boot.ps1 -- turnkey launcher for the RetroArch + Beetle Saturn live-memory
# harness. Boots game.cue detached (survives this script), holds focus so the
# emulation runs unpaused, drives it for -RunSeconds, then leaves RA running so
# the python tools (qa_netmem / qa_trace / qa_invariants / qa_memcheck) can read
# live memory over UDP 55355. Proven config (2026-07-04): 4MB cart + forward-slash
# paths + WorkingDirectory (see memory retroarch-live-memory-harness).
#
#   pwsh tools/qa_ra_boot.ps1 [-RunSeconds 75] [-Cue game.cue]
#   ... then: python tools/qa_invariants.py --live ; python tools/qa_memcheck.py --live
#   stop with: taskkill /F /IM retroarch.exe
param(
  [int]$RunSeconds = 75,
  [string]$Cue = "D:\sonicmaniasaturn\game.cue"
)
$RA = "D:\sonicmaniasaturn\tools\retroarch\RetroArch-Win64"

# ensure the 4MB cart core option (the boot-critical setting) is present
$opt = "$RA\retroarch-core-options.cfg"
if (-not (Test-Path $opt) -or -not (Select-String -Path $opt -Pattern 'Extended RAM \(4MB\)' -Quiet)) {
  'beetle_saturn_cart = "Extended RAM (4MB)"' | Set-Content $opt
  New-Item -ItemType Directory -Force "$RA\config\Beetle Saturn" | Out-Null
  Copy-Item $opt "$RA\config\Beetle Saturn\Beetle Saturn.opt" -Force
}

taskkill /F /IM retroarch.exe 2>$null | Out-Null
Start-Sleep -Seconds 1
$p = Start-Process -FilePath "$RA\retroarch.exe" -WorkingDirectory $RA `
     -ArgumentList @("--config","$RA\retroarch.cfg","-L","$RA\cores\mednafen_saturn_libretro.dll",$Cue) -PassThru
Write-Output "RetroArch launched PID $($p.Id); booting..."

Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class U{[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")]public static extern bool EnumWindows(EnumProc cb,IntPtr p);
[DllImport("user32.dll")]public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
public delegate bool EnumProc(IntPtr h,IntPtr p);}
"@
function RA-Win { $script:f=[IntPtr]::Zero; $cb=[U+EnumProc]{param($h,$q) $sb=New-Object Text.StringBuilder 256;[U]::GetWindowText($h,$sb,256)|Out-Null; if($sb.ToString() -match 'RetroArch'){$script:f=$h;return $false};return $true}; [U]::EnumWindows($cb,[IntPtr]::Zero)|Out-Null; return $script:f }
function Cmd($c){ $u=New-Object Net.Sockets.UdpClient; $u.Connect("127.0.0.1",55355); $b=[Text.Encoding]::ASCII.GetBytes($c); $u.Send($b,$b.Length)|Out-Null; $u.Client.ReceiveTimeout=1500; try{$ep=New-Object Net.IPEndPoint([Net.IPAddress]::Any,0);$r=$u.Receive([ref]$ep);$u.Close();return [Text.Encoding]::ASCII.GetString($r).Trim()}catch{$u.Close();return ""} }

Start-Sleep -Seconds 8
if ((Cmd "GET_STATUS") -match "PAUSED") { Cmd "PAUSE_TOGGLE" | Out-Null }
$ticks = [int]($RunSeconds / 2)
for ($i=0; $i -lt $ticks; $i++) {
  $w = RA-Win; if ($w -ne [IntPtr]::Zero) { [U]::SetForegroundWindow($w) | Out-Null }
  Start-Sleep -Seconds 2
}
Write-Output "ran $RunSeconds s; RA left running (status: $(Cmd 'GET_STATUS')). Read it with the python tools; stop with taskkill /F /IM retroarch.exe"
