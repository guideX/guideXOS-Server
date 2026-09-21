#!/usr/bin/env python3
"""MC5: deterministic Swoosh.wav (MS-ADPCM stereo 22050 Hz) to PCM16 mono.

The original Missile Command tree ships five PCM WAVs plus one compressed
asset: Swoosh.wav (WAVE_FORMAT_ADPCM tag 2, 2 channels, 22050 Hz, 4-bit,
blockAlign 1024, 1012 samples/block, 6656 samples/channel per the fact
chunk). The App Model audio ABI v1 carries only decoded PCM (8/16-bit mono),
so shipping the ADPCM bytes would force an ADPCM decoder into every host
and every app. Instead this script converts the project-owned source asset
once, deterministically, into a native PCM resource -- the same policy MC4
used for the build1.gif -> city.gximg art conversion:

  source (read-only):  D:/dev/bkup/inactive/missilecommand/Swoosh.wav
  derived (staged):    sdk/samples/missilecommand/resources/audio/swoosh.wav
                       (PCM16 mono 22050 Hz, 6656 frames)

Determinism: pure function of the source bytes (stdlib struct only, no
timestamps, no RNG, fixed 44-byte WAV header). The source file is never
modified. Re-running must reproduce the staged file byte-for-byte.
"""

import struct
import sys

ADAPT_TABLE = (230, 230, 230, 230, 307, 409, 512, 739,
               605, 439, 154, 45, 36, 65, 108, 178)


def read_chunks(data):
    if data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAVE file")
    pos = 12
    out = {}
    while pos + 8 <= len(data):
        ck, sz = struct.unpack("<4sI", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + sz]
        out.setdefault(ck, []).append(body)
        pos += 8 + sz + (sz & 1)
    return out


def decode_ms_adpcm(body, channels, block_align, samples_per_block,
                    coef1, coef2, total_samples):
    """Decode MS-ADPCM stereo (sequential per-channel nibbles) to int lists."""
    out = [[] for _ in range(channels)]
    pos = 0
    remaining = total_samples
    while pos < len(body) and remaining > 0:
        want = min(samples_per_block, remaining)
        preds, deltas, s1, s2 = [], [], [], []
        for _ in range(channels):
            if pos + 7 > len(body):
                raise ValueError("truncated ADPCM block header")
            preds.append(body[pos] % len(coef1))
            deltas.append(struct.unpack("<h", body[pos + 1:pos + 3])[0])
            s1.append(struct.unpack("<h", body[pos + 3:pos + 5])[0])
            s2.append(struct.unpack("<h", body[pos + 5:pos + 7])[0])
            pos += 7
        for c in range(channels):
            out[c].append(s1[c])
            if want > 1:
                out[c].append(s2[c])
        # Nibble streams are sequential per channel: all of ch0, then ch1.
        nibbles_needed = want - 2
        for c in range(channels):
            samp1, samp2 = s2[c], s1[c]
            delta = deltas[c]
            pred = preds[c]
            data_bytes = (nibbles_needed + 1) // 2
            seg = body[pos:pos + data_bytes]
            if len(seg) < data_bytes:
                raise ValueError("truncated ADPCM nibble stream")
            pos += data_bytes
            for i in range(nibbles_needed):
                byte = seg[i // 2]
                nib = (byte >> 4) & 0xF if i % 2 == 0 else byte & 0xF
                err = nib - 16 if nib >= 8 else nib
                predict = (samp1 * coef1[pred] + samp2 * coef2[pred]) // 256
                predict += err * delta
                predict = max(-32768, min(32767, predict))
                out[c].append(predict)
                samp2, samp1 = samp1, predict
                delta = (ADAPT_TABLE[nib] * delta) // 256
                if delta < 16:
                    delta = 16
        remaining -= want
    for c in range(channels):
        del out[c][total_samples:]
    return out


def main():
    if len(sys.argv) != 3:
        print("usage: convert-missilecommand-swoosh.py <Swoosh.wav> <out.wav>")
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    chunks = read_chunks(data)
    fmt = chunks[b"fmt "][0]
    tag, ch, rate, _avg, align, bits, xsz = struct.unpack("<HHIIHHH", fmt[:18])
    if tag != 2 or ch != 2 or rate != 22050 or bits != 4:
        raise ValueError("unexpected Swoosh format (not MS-ADPCM stereo 22050)")
    spb, ncoef = struct.unpack("<HH", fmt[18:22])
    coef1, coef2 = [], []
    for i in range(ncoef):
        c1, c2 = struct.unpack("<hh", fmt[22 + 4 * i:26 + 4 * i])
        coef1.append(c1)
        coef2.append(c2)
    total = struct.unpack("<I", chunks[b"fact"][0][:4])[0]
    channels = decode_ms_adpcm(chunks[b"data"][0], ch, align, spb,
                               coef1, coef2, total)
    mono = [(channels[0][i] + channels[1][i]) // 2 for i in range(total)]
    pcm = struct.pack("<%dh" % total, *mono)
    header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(pcm),
                         b"WAVE", b"fmt ", 16, 1, 1, rate, rate * 2, 2, 16,
                         b"data", len(pcm))
    with open(dst, "wb") as f:
        f.write(header)
        f.write(pcm)
    peak = max(abs(s) for s in mono)
    print("swoosh: frames=%d rate=%d peak=%d bytes=%d"
          % (total, rate, peak, len(pcm)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
