#!/usr/bin/env python3
# =============================================================================
# eval_codecs.py -- sprite-pipeline rework increment 3: MEASURED comparison of
# the Genesis-lineage codecs (Kosinski / Kosinski+ / Saxman / Comper) vs
# zlib(miniz) vs raw, over the pre-cut FRD frame-directory blobs.
#
# Format authority: flamewing/mdcomp (github.com/flamewing/mdcomp, the Sonic
# Retro community reference library, LGPL) -- decode loops transcribed
# field-for-field from:
#   kosinski.cc:157-207  (16-bit LE descriptor, LSB-first bits; literal=1+8;
#                         inline match desc 00+2bits count 2-5, dist 1-256;
#                         separate match desc 01, 13-bit dist to 8192,
#                         count 3-9 in low 3 bits, or extended byte 10-256)
#   kosplus.cc:151-206   (same shapes; separate-match count = 10-High for
#                         2-byte form, extended = byte+9; byte order High,Low)
#   comper.cc:127-155    (16-bit BE descriptor, MSB-first; WORD-oriented:
#                         literal=1+16; match=1+16 with dist 1-256 WORDS,
#                         len byte+1 words, len byte 0 = terminator)
#   saxman.cc:150-205    (byte descriptors LSB-first; literal=1+8; match=
#                         1+16, len 3-18, 12-bit absolute-in-window offset
#                         stored -0x12, window 0x1000; zero-fill form)
# Encoders here are GREEDY+lazy (hash-chain matcher). mdcomp uses an optimal
# parse, so the Genesis-codec ratios below are LOWER BOUNDS (typically a few
# percent worse than optimal). zlib -9 is exact (it IS the shipping miniz).
# Every codec round-trips byte-exact through its own decoder before its size
# is reported -- a failed round-trip aborts the run.
#
# NOTE ON CONFORMANCE: the Saturn has no pre-existing decoder for any of
# these; the SH-2 decoder would be ported from the same transcription. The
# on-CD stream only has to match OUR decoder, so self-consistency (verified
# here) is the correctness bar; byte-parity with Genesis-era streams is not
# required.
# =============================================================================
import glob
import os
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
CD = os.path.join(ROOT, "cd")


# ---------------------------------------------------------------- matcher --
def build_matches(data, window, minlen, maxlen, word=False):
    """Greedy-with-lazy LZ parse. Yields (pos, kind, a, b): kind 'lit' (a=
    unit) or 'match' (a=distance in units, b=length in units). Unit = byte,
    or 16-bit word when word=True."""
    if word:
        units = [data[i:i + 2] for i in range(0, len(data), 2)]
    else:
        units = data
    n = len(units)
    head = {}
    chains = {}

    def key(i):
        if word:
            return (units[i], units[i + 1]) if i + 1 < n else None
        return bytes(units[i:i + 3]) if i + 3 <= n else None

    def find(i):
        k = key(i)
        best_len = 0
        best_dist = 0
        if k is None:
            return 0, 0
        tries = 64
        j = head.get(k, -1)
        while j >= 0 and tries > 0:
            dist = i - j
            if dist > window:
                break
            l = 0
            lim = min(maxlen, n - i)
            while l < lim and units[j + l] == units[i + l]:
                l += 1
            if l > best_len:
                best_len = l
                best_dist = dist
                if l >= lim:
                    break
            j = chains.get(j, -1)
            tries -= 1
        return best_len, best_dist

    def insert(i):
        k = key(i)
        if k is not None:
            chains[i] = head.get(k, -1)
            head[k] = i

    i = 0
    out = []
    while i < n:
        l, d = find(i)
        if l >= minlen:
            # lazy: is the parse better starting at i+1?
            l2, d2 = find(i + 1) if i + 1 < n else (0, 0)
            if l2 > l + 1:
                out.append((i, "lit", units[i], 0))
                insert(i)
                i += 1
                continue
            out.append((i, "match", d, l))
            for k2 in range(i, i + l):
                insert(k2)
            i += l
        else:
            out.append((i, "lit", units[i], 0))
            insert(i)
            i += 1
    return out


# --------------------------------------------------------------- kosinski --
class BitsLE:
    """Kosinski 16-bit LE descriptor words, bits consumed LSB-first.
    Encoder groups each descriptor word before the data bytes its 16 bits
    describe (self-consistent with the decoder below)."""
    def __init__(self):
        self.groups = []       # (descriptor-bits list, bytes)
        self.bits = []
        self.data = bytearray()

    def bit(self, b):
        # LAZY flush: bytes emitted after the 16th bit (the tail of that
        # command) must stay in the CURRENT group -- the decoder reads the
        # next descriptor word only when a 17th bit is requested.
        if len(self.bits) == 16:
            self.flush()
        self.bits.append(b)

    def byte(self, b):
        self.data.append(b)

    def flush(self):
        if not self.bits and not self.data:
            return
        v = 0
        for i, b in enumerate(self.bits):
            v |= b << i
        self.groups.append((struct.pack("<H", v), bytes(self.data)))
        self.bits = []
        self.data = bytearray()

    def blob(self):
        self.flush()
        return b"".join(d + p for d, p in self.groups)


class BitsLERead:
    def __init__(self, blob):
        self.b = blob
        self.pos = 0
        self.nbits = 0
        self.word = 0

    def bit(self):
        if self.nbits == 0:
            self.word, = struct.unpack_from("<H", self.b, self.pos)
            self.pos += 2
            self.nbits = 16
        v = self.word & 1
        self.word >>= 1
        self.nbits -= 1
        return v

    def byte(self):
        v = self.b[self.pos]
        self.pos += 1
        return v


def kos_encode(data, plus=False):
    parse = build_matches(data, 8192, 2, 256)
    o = BitsLE()
    for (_i, kind, a, b) in parse:
        if kind == "lit":
            o.bit(1)
            o.byte(a)
        else:
            dist, ln = a, b
            if dist <= 256 and 2 <= ln <= 5:
                o.bit(0); o.bit(0)
                if plus:
                    # kosplus.cc:189-194 -- offset byte BETWEEN the selector
                    # and the two count descriptor bits
                    o.byte((0x100 - dist) & 0xFF)
                    o.bit((ln - 2) >> 1); o.bit((ln - 2) & 1)
                else:
                    o.bit((ln - 2) >> 1); o.bit((ln - 2) & 1)
                    o.byte((0x100 - dist) & 0xFF)
            else:
                if ln == 2:  # 2-long far match not encodable: emit literals
                    o.bit(1); o.byte(data[_i])
                    o.bit(1); o.byte(data[_i + 1])
                    continue
                o.bit(0); o.bit(1)
                off = 0x2000 - dist
                low = off & 0xFF
                hi5 = (off >> 5) & 0xF8
                if not plus and 3 <= ln <= 9:
                    o.byte(low); o.byte(hi5 | (ln - 2))
                elif plus and 3 <= ln <= 9:
                    o.byte(hi5 | (10 - ln)); o.byte(low)
                elif not plus:
                    o.byte(low); o.byte(hi5); o.byte(ln - 1)
                else:
                    o.byte(hi5); o.byte(low); o.byte(ln - 9)
    # terminator
    o.bit(0); o.bit(1)
    if plus:
        o.byte(0xF0); o.byte(0x00); o.byte(0x00)
    else:
        o.byte(0x00); o.byte(0xF0); o.byte(0x00)
    return o.blob()


def kos_decode(blob, plus=False):
    src = BitsLERead(blob)
    out = bytearray()
    while True:
        if src.bit():
            out.append(src.byte())
            continue
        if src.bit():
            if plus:
                high = src.byte(); low = src.byte()
            else:
                low = src.byte(); high = src.byte()
            count = high & 7
            if count == 0:
                count = src.byte()
                if count == 0:
                    break
                if not plus and count == 1:
                    continue
                count = count + 1 if not plus else count + 9
            else:
                count = count + 2 if not plus else 10 - count
            dist = 0x2000 - (((high & 0xF8) << 5) | low)
        else:
            if plus:
                dist = 0x100 - src.byte()
                hi = src.bit(); lo = src.bit()
            else:
                hi = src.bit(); lo = src.bit()
                dist = 0x100 - src.byte()
            count = ((hi << 1) | lo) + 2
        for _ in range(count):
            out.append(out[-dist])
    return bytes(out)


# ----------------------------------------------------------------- comper --
def comper_encode(data):
    if len(data) & 1:
        data = data + b"\0"
    parse = build_matches(data, 256, 2, 256, word=True)
    bits = []
    payload = []
    groups = []

    def emit(bit, by):
        if len(bits) == 16:   # lazy flush (see BitsLE.bit)
            flush()
        bits.append(bit)
        payload.append(by)

    def flush():
        if not bits:
            return
        v = 0
        for i, b in enumerate(bits):
            v |= b << (15 - i)
        groups.append(struct.pack(">H", v) + b"".join(payload))
        del bits[:]
        del payload[:]

    for (_i, kind, a, b) in parse:
        if kind == "lit":
            emit(0, a)
        else:
            emit(1, bytes([(0x100 - a) & 0xFF, b - 1]))
    emit(1, b"\0\0")  # terminator: length byte 0
    flush()
    return b"".join(groups)


def comper_decode(blob):
    pos = 0
    out = bytearray()
    while True:
        word, = struct.unpack_from(">H", blob, pos)
        pos += 2
        done = False
        for i in range(16):
            if word & (0x8000 >> i):
                dist = (0x100 - blob[pos]) * 2
                ln = blob[pos + 1]
                pos += 2
                if ln == 0:
                    done = True
                    break
                for _ in range((ln + 1) * 2):
                    out.append(out[-dist])
            else:
                out += blob[pos:pos + 2]
                pos += 2
        if done:
            break
    return bytes(out)


# ----------------------------------------------------------------- saxman --
def saxman_encode(data):
    parse = build_matches(data, 0xFFF, 3, 18)
    bits = []
    payload = []
    groups = []

    def emit(bit, by):
        if len(bits) == 8:    # lazy flush (see BitsLE.bit)
            flush()
        bits.append(bit)
        payload.append(by)

    def flush():
        if not bits:
            return
        v = 0
        for i, b in enumerate(bits):
            v |= b << i        # LSB-first byte descriptors
        groups.append(bytes([v]) + b"".join(payload))
        del bits[:]
        del payload[:]

    for (i, kind, a, b) in parse:
        if kind == "lit":
            emit(1, bytes([a]))
        else:
            src = i - a                       # absolute source position
            stored = (src - 0x12) & 0xFFF
            emit(0, bytes([stored & 0xFF,
                           ((stored >> 4) & 0xF0) | (b - 3)]))
    flush()
    body = b"".join(groups)
    # stock Saxman uses a u16 size header (it shipped for <64KB sound data);
    # u32 here so >64KB blobs measure -- a real deployment would chunk.
    return struct.pack("<I", len(body)) + body


def saxman_decode(blob):
    size, = struct.unpack_from("<I", blob, 0)
    pos = 4
    end = 4 + size
    out = bytearray()
    nbits = 0
    desc = 0
    while pos < end:
        if nbits == 0:
            desc = blob[pos]; pos += 1; nbits = 8
            if pos >= end:
                break
        bit = desc & 1
        desc >>= 1
        nbits -= 1
        if bit:
            out.append(blob[pos]); pos += 1
        else:
            off = blob[pos]
            ln = blob[pos + 1]
            pos += 2
            off |= (ln << 4) & 0xF00
            ln = (ln & 0xF) + 3
            off = (off + 0x12) & 0xFFF
            base = len(out)
            src = ((off - base) % 0x1000) + base - 0x1000
            if 0 <= src < base:
                for k in range(ln):
                    out.append(out[src + k])
            else:
                out += b"\0" * ln
    return bytes(out)


# ------------------------------------------------------------------- main --
def main():
    frds = sorted(glob.glob(os.path.join(CD, "*.FRD")))
    if not frds:
        print("no FRD blobs -- run build_frame_dir.py first")
        return 1
    codecs = [
        ("zlib-9", lambda d: zlib.compress(d, 9), zlib.decompress),
        ("kosinski", lambda d: kos_encode(d, False), lambda b: kos_decode(b, False)),
        ("kosplus", lambda d: kos_encode(d, True), lambda b: kos_decode(b, True)),
        ("saxman", saxman_encode, saxman_decode),
        ("comper", comper_encode, comper_decode),
    ]
    tot = {name: 0 for name, _e, _d in codecs}
    tot_raw = 0
    print("%-14s %9s | %9s %9s %9s %9s %9s" %
          ("blob", "raw", "zlib-9", "kosinski", "kosplus", "saxman", "comper"))
    for p in frds:
        data = open(p, "rb").read()
        tot_raw += len(data)
        sizes = []
        for name, enc, dec in codecs:
            t0 = time.time()
            c = enc(data)
            rt = dec(c)
            # comper pads odd input by one byte
            if name == "comper" and len(data) & 1:
                rt = rt[:len(data)]
            if rt[:len(data)] != data or len(rt) < len(data):
                print("ROUND-TRIP FAIL: %s %s" % (os.path.basename(p), name))
                return 1
            sizes.append(len(c))
            tot[name] += len(c)
        print("%-14s %9d | %s" % (os.path.basename(p), len(data),
                                  " ".join("%9d" % s for s in sizes)))
    print("%-14s %9d | %s" % ("TOTAL", tot_raw,
                              " ".join("%9d" % tot[n] for n, _e, _d in codecs)))
    print("%-14s %9s | %s" % ("ratio", "",
                              " ".join("%8.1f%%" % (100.0 * tot[n] / tot_raw)
                                       for n, _e, _d in codecs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
