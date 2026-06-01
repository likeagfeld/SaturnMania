# DTS96 Master Index — authoritative document inventory

Phase 1.10 working document.  Subsystem-grouped catalogue of every doc
reachable from this workstation that could plausibly explain the VDP1
sprite-zero-pixels bug.  Built by `ls`-ing the three roots below and
then grouping by subsystem.  Source-of-truth so that future audits do
NOT pick "the first relevant doc" — they pick the AUTHORITATIVE doc.

Roots:
- `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\` (111 PDFs/MDs)
- `D:\Claude Saturn Skill Documentation\NOV96_DTS\` (NOV96 dev kit drop)
- `D:\Claude Saturn Skill Documentation\SBL601\SBL6\SEGASMP\` (SBL 6.01 sample programs)
- `C:\Users\gary\.claude\skills\sega-saturn-developer\references\` (26 curated MDs)

Legend in 1-line topic column: `[CITE]` = doc has the load-bearing register/bit definition for at least one §11.16 hypothesis row.

---

## 1. VDP1 (sprite/polygon/framebuffer) — bug locus

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-013-R3-061694.pdf` | ST-013-R3 | VDP1 User's Manual | [CITE] Register map (TVMR/FBCR/PTMR/EWDR/EWLR/EWRR/EDSR/COPR/MODR), command-table format, framebuffer config |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-013-SP1-052794.pdf` | ST-013-SP1 | VDP1 User's Manual Supplement | [CITE] Errata + clarifications to ST-013-R3 |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\TUTORIAL.pdf` | n/a | VDP1 - The Sprite Chip Tips and Tricks | Sprite rendering tutorial / common pitfalls |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\vdp1-reference.md` | curated | VDP1 reference (skill MD) | [CITE] Quick-reference for VDP1 init order, register semantics |

## 2. VDP2 (background/scroll/compositor/CRAM) — composite path

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-058-R2-060194.pdf` | ST-058-R2 | VDP2 User's Manual v1.1 | [CITE] Full register map, SPCTL/PRISA/PRINA/PRINB/TVMD/BGON/CCCTL/SDCTL, sprite-vs-NBG composite rules |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\vdp2-reference.md` | curated | VDP2 reference (skill MD) | [CITE] NBG/RBG bring-up cookbook, CRAM access conventions |

## 3. SCU (DMA / interrupts / bus arbitration)

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-097-R5-072694.pdf` | ST-097-R5 | SCU User's Manual (Third version) | [CITE] DMA Lvl0/1/2, IMS/IST interrupt mask/status, A-bus/B-bus arbitration, cache-through alias rules |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\SCUUsersManualSaturnUSManual.pdf` | n/a | SCU User's Manual (US edition) | US localized; 189 pages, equivalent content |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-210-110194.pdf` | ST-210 | SCU Final Specifications: Precautions | Errata for SCU |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-240-A-042795.pdf` | ST-240-A | SCU DSP Assembler Manual | DSP microcode tooling — not on bug path |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-240-A-SP1-052295.pdf` | ST-240-A-SP1 | DSP Assembler Addendum | Not on bug path |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-240-B-042795.pdf` | ST-240-B | SCU DSP Simulator Manual | Not on bug path |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-240-B-SP1-052295.pdf` | ST-240-B-SP1 | DSP Simulator Addendum | Not on bug path |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\scu-reference.md` | curated | SCU reference (skill MD) | DMA channel selection, vblank-IN interrupt source |

## 4. SH-2 CPU

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\h12p0.pdf` | n/a | SH-1/SH-2 Programming Manual | Instruction set; not on bug path |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\sh7604.pdf` | n/a | SH7604 Hardware Manual | On-chip cache, DMAC, interrupt controller, division unit |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\e702090_superh.pdf` | n/a | SuperH Cross Assembler | Tooling — not on bug path |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\shccomp.pdf` | n/a | SH C Compiler User's Manual | Tooling |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\hserlib.pdf` | n/a | H Series Librarian | Tooling |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\e702186_shsimulator.pdf` | n/a | SH Simulator/Debugger | Tooling |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\shsimdbg.pdf` | n/a | SH Series Simulator/Debugger | Tooling |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\Hitachi_-_SH_Series_Cross_Assembler_-_User_Manual.pdf` | n/a | SH Cross Assembler manual | Tooling |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\cpu-reference.md` | curated | CPU reference (skill MD) | SH-2 quick reference, cache flush rules |

## 5. SCSP (sound processor)

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-077-R2-052594.pdf` | ST-077-R2 | SCSP User's Manual | 32 sound slots, FM/PCM, DSP |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-069-121693.pdf` | ST-069 | SCSP/DSP Effect Modules | Effects |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-081-R5-062894.pdf` | ST-081-R5 | Sound Development Manual v1.1 | Sound dev workflow |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-166-R4-012395.pdf` | ST-166-R4 | Sound Driver System Interface v3.03 | SH-2 ↔ 68000 sound driver API |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-241-042795.pdf` | ST-241 | Sound Driver Implementation Manual | Implementation notes |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-198-R1-121594.pdf` | ST-198-R1 | Sound Tools Manual Supplement | Tools supplement |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-TECH-51.pdf` | ST-TECH-51 | Technical Bulletin #51 | 68000/SCSP restrictions, prohibited instructions |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\sound-reference.md` | curated | Sound reference (skill MD) | Sound subsystem quick ref |
| (other sound tools: ST-065-R1, ST-066, ST-067, ST-068-R1, ST-070-R1, ST-099-R1, ST-168-R3, ST-227-R1, ST-235) | various | Sound tools | Tooling — not on bug path |

## 6. SMPC (peripherals, RTC, system manager)

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-169-R1-072694.pdf` | ST-169-R1 | SMPC User's Manual | INTBACK, peripheral scan, RTC, CKCHG |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-214-111594.pdf` | ST-214 | SMPC Sample Program Manual | Sample code |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-TECH-42.pdf` | ST-TECH-42 | Tech Bulletin #42 | SMPC cautions |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-TECH-44.pdf` | ST-TECH-44 | Tech Bulletin #44 | Shuttle Mouse data format |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-TECH-45.pdf` | ST-TECH-45 | Tech Bulletin #45 | Keyboard data format |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-TECH-46.pdf` | ST-TECH-46 | Tech Bulletin #46 | Data Cartridge specs |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-TECH-47.pdf` | ST-TECH-47 | Tech Bulletin #47 | Extended RAM Cartridge specs |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\smpc-reference.md` | curated | SMPC reference (skill MD) | SMPC quick ref |

## 7. SBL Program Library Guides

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-136-R2-093094.pdf` | ST-136-R2 | Program Library 1: CD Library | SBL CD access API |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-157-R1-092994.pdf` | ST-157-R1 | Program Library 2: Graphics Library | [CITE] SBL VDP1/VDP2 library API — alternative to SGL |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-135-R4-092295.pdf` | ST-135-R4 | Program Library 3 | Additional SBL functions |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-136-D-R2-082495.pdf` | ST-136-D-R2 | Branching Playback Library | Interactive playback |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-162-062094.pdf` | ST-162 | System Library User's Guide | SMPC/CD-COMM/system libraries |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-162-R1-092994.pdf` | ST-162-R1 | System Library User's Guide v1.0 | Updated system lib |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\libraries-reference.md` | curated | Libraries reference (skill MD) | SBL/SGL/SRL choice + entry points |

## 8. SGL (Sega Graphics Library) — what jo actually wraps

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-238-R1-051795.pdf` | ST-238-R1 | SGL Developer's Manual Reference | [CITE] Complete sl*() API reference, slInitSystem, slDispSprite, slSynch, slScrAutoDisp |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-237-R1-051795.pdf` | ST-237-R1 | SGL Developer's Manual Tutorial | [CITE] Programmer's tutorial; sample code structure |
| `D:\Claude Saturn Skill Documentation\ST-238-R1-051795.pdf` | (duplicate) | (same as above) | (same) |
| `D:\Claude Saturn Skill Documentation\ST-151-R4-020197.pdf` | ST-151-R4 | Saturn Software Development Standards | TRC checklist (not on bug path) |
| `D:\sonicmaniasaturn\jo-engine\Compiler\COMMON\SGL_302j\INC\SGL.H` | source | SGL master header | Function declarations, SPRITE/SPR_ATTR types, scrSPR/scnNBG enums |
| `D:\sonicmaniasaturn\jo-engine\Compiler\COMMON\SGL_302j\INC\SL_DEF.H` | source | SGL low-level defs | [CITE] SPRON/NBG2ON/etc. bit positions, SPR_ATTRIBUTE macro, TV_320x240/256 enum, sprNoflip/FUNC_Sprite/CL32KRGB |
| `D:\sonicmaniasaturn\jo-engine\Compiler\COMMON\SGL_302j\INC\SL_MACRO.H` | source | SGL macros | Helper macros |
| `D:\sonicmaniasaturn\jo-engine\Compiler\COMMON\SGL_302j\INC\SS_SCROL.H` | source | SGL scroll-screen defs | NBG cycle pattern, plane size |

## 9. Sound Driver — orthogonal to bug path

(See §5 above — ST-166-R4 / ST-241.)

## 10. CD-ROM / CD Library / disc format

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\External_Specifications_-_Saturn_CD_Communication_Interface.pdf` | ST-38-R1 | CD Communication Interface | CD block register interface |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\External_Specifications_-_Saturn_File_System_Library.pdf` | ST-39-R2 | File System Library | File access API |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-098-031194.pdf` | ST-098 | Saturn Stream System | Streaming |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-129-R2-093094.pdf` | ST-129-R2 | Virtual CD System | Dev tooling |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-129-R2-SP1-061995.pdf` | ST-129-R2-SP1 | Virtual CD System Supplement | Supplement |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-129-R2-SP2-082495.pdf` | ST-129-R2-SP2 | MPEG Stream Build Precautions | Video build |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-182-081294.pdf` | ST-182 | Virtual CD System Release 3 Limitations | Tooling limits |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-201-B-092994.pdf` | ST-201-B | Write-Once CD-R System | Mastering |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-211-110494.pdf` | ST-211 | CD Development Tool | CD dev tool |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-040-R4-051795.pdf` | ST-040-R4 | Disc Format Standards v1.0 | ISO 9660 / session layout |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\cd-backup-reference.md` | curated | CD + backup ref (skill MD) | Quick ref |

## 11. Dual CPU programming

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-202-R1-120994.pdf` | ST-202-R1 | Dual CPU User's Guide | Master/slave coordination |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\dual-cpu-reference.md` | curated | Dual CPU reference (skill MD) | Quick ref |

## 12. Boot ROM / IP.BIN / power-on

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-079B-R3-011895.pdf` | ST-079B-R3 | Boot ROM User's Manual | Boot sequence, IP.BIN |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\Saturn_Boot_ROM_v0.8_-_Floppy_Disk_Information.pdf` | n/a | Boot ROM v0.8 Floppy Info | Dev setup |

## 13. Backup RAM

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-203-100494.pdf` | ST-203 | Backup System Production Standard | Save format |

## 14. Graphics References

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-103-R1-040194.pdf` | ST-103-R1 | Saturn/32X Graphics References v2.0 | Visual prog reference |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-124-R1-091394.pdf` | ST-124-R1 | Saturn/32X Graphics References v2.0 (update) | Updated graphics ref |

## 15. Tech Bulletins (full archive)

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\Sattechs.pdf` | TB #1-#41 + SOA #1-#12 | Combined Technical Bulletins | Full archive |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-TECH.pdf` | TB #1-3 | Tech Bulletins 1-3 | CD drive duty, audio pre-emphasis |
| `D:\Claude Saturn Skill Documentation\ST-TECH-1-41.pdf` | (duplicate of Sattechs.pdf) | (same) | (same) |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-TECH-48.pdf` | TB #48 | Tech Bulletin #48 | Dev tools categorization |

## 16. DTS Developer Newsletters (hardware errata source)

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\DTS-1094.pdf` | n/a | Newsletter Oct 1994 | Errata + tips |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\DTS-0195.pdf` | n/a | Newsletter Jan 1995 | Errata + tips |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\DTS-0695.pdf` | n/a | Newsletter Jun 1995 | Errata + tips |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\DTS-0995.pdf` | n/a | Newsletter Sep 1995 | Errata + tips |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\DTS-0496.pdf` | n/a | Newsletter Apr 1996 | Errata + tips |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\DTS-0596.pdf` | n/a | Newsletter May 1996 | Errata + tips |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\DTS-0696.pdf` | n/a | Newsletter Jun/Jul 1996 | Errata + tips |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\DTS-1196.pdf` | n/a | Newsletter Nov 1996 | Errata + tips |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\DTS-0197.pdf` | n/a | Newsletter Jan/Feb 1997 | Errata + tips |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\infochunk_developer_info.md` | curated | Developer Info Bulletins x52 | VDP1/VDP2 errata, SCU DMA, peripherals |

## 17. Misc samples / overview

| file path | doc ID | title | 1-line topic |
|---|---|---|---|
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\13-APR-94.pdf` | n/a | Saturn Game Dev Intro | High-level overview |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-155-062094.pdf` | ST-155 | Saturn Introduction Manual | Setup |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-159-R1-092994.pdf` | ST-159-R1 | Sample Game Program Manual | Official sample game |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\ST-160-R1-092994.pdf` | ST-160-R1 | Sample Data Manual | Sample data |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\siff.pdf` | n/a | SIFF File Format | Video format |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\SOUND.pdf` | n/a | DTS Sound Development Document | Sound overview |
| `D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\infochunk_faq.md` | curated | Developer FAQ | Practical troubleshooting |

## 18. NOV96_DTS sample programs

Under `D:\Claude Saturn Skill Documentation\NOV96_DTS\EXAMPLES\`:

- `SGL\BIPLANE\` — biplane sample (slCurUserSystem / sprite rendering pattern, referenced from archived main.c §3.2)
- `SGL\CDDA_SGL\` — CD-DA + SGL integration
- `SGL\CHROME\` — chrome-effect sample
- `SGL\FLYING\` — flying sample
- `DEVCON96\3D\` — 3D sample
- `DEVCON96\DEMOCOEF\` `DEMODSP\` `DEMODUAL\` `DEMODUL2\` `DEMODUL3\` `DEMOGOUR\` `DEMOMIST\` `DEMOTB14\` `DEMOWIN\` — DEVCON96 demo suite
- `DUALCPU\` — dual SH-2 sample
- `BACKRAM\` — backup RAM sample
- `GFSDEMO\` — file system sample
- `PERIPHS\` — peripherals sample

## 19. SBL601 sample programs

Under `D:\Claude Saturn Skill Documentation\SBL601\SBL6\SEGASMP\`:

`ABS BUP COF CSH DBG DMA DUAL FLD GAME GAME_CD GFS LIB MAN MEM MPG PCM PER SBLSGL SCL SGL SND SPR STM SYS TIM V_BLANK`

Of interest for VDP1 audit: `SPR` (sprite samples), `V_BLANK` (vblank-handling samples), `SBLSGL` (SBL+SGL coexistence), `DMA` (DMA samples).

## 20. Skill curated reference MDs

| file path | 1-line topic |
|---|---|
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\official-docs-reference.md` | Master DTS PDF index |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\vdp1-reference.md` | [CITE] VDP1 |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\vdp2-reference.md` | [CITE] VDP2 |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\scu-reference.md` | SCU |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\cpu-reference.md` | SH-2 |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\smpc-reference.md` | SMPC |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\sound-reference.md` | SCSP |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\libraries-reference.md` | SBL/SGL/SRL |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\joengine-reference.md` | [CITE] Jo Engine reference |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\saturnsdk-reference.md` | Saturn SDK quick ref |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\toolchain-reference.md` | Toolchain |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\dual-cpu-reference.md` | Dual CPU |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\cd-backup-reference.md` | CD + backup |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\netlink-reference.md` | NetLink |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\custom-networking-reference.md` | Custom networking |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\mmm-netlink-reference.md` | MMM NetLink |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\utenyaa-netlink-reference.md` | Utenyaa NetLink |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\coup-reference.md` | Coup NetLink sample |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\faq-reference.md` | [CITE] FAQ — practical issues including VDP1 sprite-lag |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\srl-reference.md` | SRL library |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\saturnmathpp-reference.md` | SaturnMath++ |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\rsdk-mania-port-reference.md` | RSDK Mania port reference |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\video-reference.md` | Video / Cinepak |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\online-conversion-playbook.md` | Online conversion |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\game-audio-codec-identification.md` | Audio codec ID |
| `C:\Users\gary\.claude\skills\sega-saturn-developer\references\game-voice-dubbing-speaker-attribution.md` | Voice dubbing |

## 21. Authoritative-doc quick-pick by Phase 1.10 §11.16 hypothesis row

| §11.16 row | Authoritative doc | Why |
|---|---|---|
| VDP1 TVMR (0x25D00000) | ST-013-R3 §VDP1 System Registers — TVMR | Bit fields VBE/HTV/IE/TVM, page-precise definition |
| VDP1 FBCR (0x25D00002) | ST-013-R3 §FBCR | Framebuffer change mode + DIE/DIL/FCM/FCT bits |
| VDP1 PTMR (0x25D00004) | ST-013-R3 §PTMR | Plot trigger: 00=idle 01=plot-once 10=auto |
| VDP1 EWDR/EWLR/EWRR (0x25D00006/8/A) | ST-013-R3 §Erase Window registers | Erase rect defaults; misprogrammed = blank framebuffer |
| VDP1 EDSR (0x25D00010) | ST-013-R3 §EDSR | Transfer end + change end status |
| VDP1 command table at 0x25C00000 | ST-013-R3 §Command Tables | 32-byte command format CMDCTRL/CMDLINK/CMDPMOD/CMDCOLR/CMDSRCA/CMDSIZE/CMDXA-D/YA-D/CMDGRDA |
| VDP2 SPCTL (0x25F800E0) | ST-058-R2 §SPCTL | SPCLMD bit 5 + SPTYPE bits 0-3 |
| VDP2 SPRPRI banks (0x25F800F0..F6) | ST-058-R2 §Sprite Priority | SnPRIN fields, bank-select |
| VDP2 PRINA/B (0x25F800F8/FA) | ST-058-R2 §NBG Priority | NnPRIN fields |
| SCU IST/IMS (0x25FE00A0/A4) | ST-097-R5 §Interrupt Controller | Bit positions for vblank-IN, vblank-OUT, hblank-IN |
| SGL slInitSystem / SortList | ST-238-R1 §System Library — slInitSystem | Required args, init order, what it programs |
| SGL slDispSprite | ST-238-R1 §Sprite Functions — slDispSprite | Queue command-list semantics |
| slSynch | ST-238-R1 §System Library — slSynch | Per-frame flush; what it actually does |
| slScrAutoDisp | ST-238-R1 §VDP2 Functions — slScrAutoDisp | Screen-on flags including SPRON |
| jo_sprite_init / __jo_sprite_id reset | (source) jo-engine/jo_engine/sprites.c:97-120 | Whether sprite list resets per-frame |

---

End of master index.
