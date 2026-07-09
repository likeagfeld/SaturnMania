#!/usr/bin/env python3
"""qa_ghz_gameplay_cd_silence_gate.py - "zero CD access during GHZ gameplay".

WHY THIS GATE EXISTS (the bug it catches): GHZ music is CD-DA track 2
(src/mania/Game.c jo_audio_play_cd_track(2,2,true)). The Saturn CD has ONE
head, so ANY data-sector read during play seeks the head off the audio track
-> music/SFX drop AND the main loop stalls waiting on GFS -> the user-reported
"super slow gameplay, no level music, no SFX, invisible HUD" once the GHZ
title-card appears.

Task #180 step 4c removed ONE of three gameplay-time CD readers (the collision
columns -> now RAM-resident, see tools/qa_ghz_cd_contention_gate.py). The
biggest remaining contributor was the PLAYER atlas:

  src/rsdk/player_atlas.c  _cache_load() -> jo_fs_read_file_ptr  (SONICnn.SP2,
                           on every Sonic animation change / MRU miss)

Task #192 ("player-only resident", user decision 2026-06-02) makes ONLY the
player resident: SONIC.SPC is loaded once at scene-load into the free LWRAM
tail (player_atlas.c), so the frequent per-anim player CD reads are gone. The
entity atlas was REVERTED to CD streaming on purpose -- the resident entity
pack byte-collided with the live #188 FG.CEL region and crashed the foreground
after the title card; entity reads are occasional (first display / MRU evict)
and far rarer than the player's per-anim reads, an acceptable residual.

The contract THIS gate now enforces: the PLAYER per-frame path must NEVER touch
the CD. The ONLY CD read permitted in player_atlas.c is the scene-load-time
read inside player_atlas_load(). Any CD-read token attributed to ANY OTHER
function in player_atlas.c is a gameplay-time player CD reader -> RED.
entity_atlas.c is intentionally NOT policed here (streaming is by design).

Pure host file parse - no Saturn/emulator dependency; runs in verify_done. The
function-attribution technique mirrors qa_ghz_cd_contention_gate.py P3.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (source file, set of functions in which a CD read is PERMITTED). Everything
# else is a gameplay-time path and must be CD-silent.
TARGETS = [
    (os.path.join(ROOT, "src", "rsdk", "player_atlas.c"), {"player_atlas_load"}),
]

# Every primitive that ultimately seeks/reads the CD head, as a regex so we can
# match jo_fs_read_file WITHOUT also counting its jo_fs_read_file_ptr superset
# (negative lookahead). Each entry is matched independently and de-duplicated by
# byte offset so the same call site is never tallied twice.
CD_TOKEN_RES = (
    r"jo_fs_read_file_ptr",
    r"jo_fs_read_file(?!_ptr)",
    r"GFS_Fread",
    r"GFS_Seek",
    r"GFS_Load",
    r"GFS_NwFread",
    r"slCdRead",
)


def _blank(m):
    # Replace a comment with same-length spaces so byte offsets (and the
    # nearest-preceding-header attribution) stay aligned; CD tokens that only
    # appear in prose must not count as real calls.
    return re.sub(r"[^\n]", " ", m.group(0))


def attribute(src):
    """Return ordered [(offset, func_name)] for every C function definition
    header: a line-anchored '[static] rettype name(args)'. CD tokens only
    appear inside bodies and headers are sequential, so each token belongs to
    the nearest header that precedes it."""
    headers = [(m.start(), m.group(1)) for m in re.finditer(
        r"(?m)^(?:static\s+)?[A-Za-z_][\w\*\s]*?\b(\w+)\s*\([^;{]*\)\s*$", src)]
    headers.sort()
    return headers


def func_at(headers, pos):
    best = None
    for off, name in headers:
        if off < pos:
            best = name
        else:
            break
    return best


def scan(path, allowed):
    if not os.path.exists(path):
        return ["missing source: %s" % path]
    raw = open(path, "r", encoding="utf-8", errors="replace").read()
    src = re.sub(r"/\*.*?\*/|//[^\n]*", _blank, raw, flags=re.DOTALL)
    headers = attribute(src)
    bad = []
    seen = set()
    for pat in CD_TOKEN_RES:
        for m in re.finditer(pat, src):
            i = m.start()
            if i in seen:
                continue
            seen.add(i)
            fn = func_at(headers, i)
            if fn not in allowed:
                bad.append("%s() calls %s (gameplay-time CD read)"
                           % (fn or "?", m.group(0)))
    return bad


def main():
    print("=== Task #180 follow-up: GHZ gameplay CD-silence gate ===")
    fails = []
    for path, allowed in TARGETS:
        rel = os.path.relpath(path, ROOT)
        bad = scan(path, allowed)
        if bad:
            for b in bad:
                fails.append("%s: %s" % (rel, b))
        else:
            print("  OK  %s: CD reads confined to %s"
                  % (rel, "/".join(sorted(allowed))))
    print("")
    if fails:
        print("=== GATE RED (%d gameplay-time CD reader%s): ==="
              % (len(fails), "" if len(fails) == 1 else "s"))
        for f in fails:
            print("    - %s" % f)
        print("  -> single-head CD-DA (GHZ music track 2) is interrupted "
              "every time one of these fires -> music/SFX drop + main-loop "
              "stall (super-slow gameplay).")
        return 1
    print("=== GATE GREEN: the player per-frame path is CD-silent (SONIC.SPC "
          "resident); the frequent per-anim player reads no longer interrupt "
          "CD-DA music. (Entity SP2 streaming is occasional + by design.) ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
