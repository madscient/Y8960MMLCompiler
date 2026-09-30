import argparse, pathlib, re

# The ROM repository is expected beside this one. --rom points elsewhere.
parser = argparse.ArgumentParser(description="Generate src/core/voicedata.cpp from the ROM's tables.")
parser.add_argument("output", help="the .cpp file to write")
parser.add_argument("--rom", type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parents[2] / "Y8960BasicExtension",
                    help="the Y8960BasicExtension checkout (default: beside this repository)")
args = parser.parse_args()
ROM = args.rom / "src" / "tab"

def parse_db(path, label, comment_names=False):
    """Collect the bytes of every `db` between `label:` and the next label.

    A record's name is its quoted run, or with comment_names the comment line
    above it: the rhythm voices are packed, and the packed form has no name.
    """
    lines = path.read_text(encoding="ascii").splitlines()
    out, names, started = [], [], False
    for ln in lines:
        stripped = ln.strip()
        if not started:
            if stripped.startswith(label + ":"):
                started = True
            continue
        if re.match(r"^[A-Za-z_][A-Za-z0-9_]*:", stripped):
            break
        if comment_names:
            c = re.match(r"^;\s*(\S.*)$", stripped)
            if c:
                names.append(c.group(1))
                continue
        m = re.match(r"^db\s+(.*)$", stripped, re.I)
        if not m:
            continue
        body = m.group(1)
        # a quoted run is the eight character name
        q = re.match(r"^'(.*)'\s*$", body)
        if q:
            names.append(q.group(1))
            out.extend(ord(c) for c in q.group(1))
            continue
        for tok in body.split(","):
            tok = tok.strip()
            if re.match(r"^[0-9A-Fa-f]+[Hh]$", tok):
                out.append(int(tok[:-1], 16) & 0xFF)
            else:
                out.append(int(tok, 10) & 0xFF)
    return out, names

voices, vnames = parse_db(ROM / "voicedat.asm", "VOICETAB")
rhythm, rnames = parse_db(ROM / "voicedat.asm", "VOICERTM", comment_names=True)
waves, _ = parse_db(ROM / "wavedat.asm", "WAVETAB")

assert len(voices) == 64 * 32, len(voices)
assert len(rhythm) == 3 * 12 and len(rnames) == 3, (len(rhythm), rnames)
assert len(waves) == 16 * 32, len(waves)

BASIC_ROWS = (8, 8, 8, 8)
PACKED_ROWS = (2, 5, 5)  # FB/CNT and transpose, then each operator

def table(name, data, rows, comments):
    stride = sum(rows)
    lines = [f"const std::uint8_t {name}[] = {{"]
    for i in range(0, len(data), stride):
        lines.append(f"    // {i // stride:2d} {comments[i // stride]}")
        at = i
        for n in rows:
            lines.append("    " + " ".join(f"0x{b:02X}," for b in data[at:at + n]))
            at += n
    lines.append("};")
    return "\n".join(lines)

hdr = """// Generated from Y8960BasicExtension src/tab/voicedat.asm and src/tab/wavedat.asm
// by tools/gen_voicedata.py. Do not edit by hand.
//
// The FM voice table is not this project's own work. See the rights section of
// README.md: the presets come from YAMAHA and ASCII by way of MSX-AUDIO BASIC
// Extension Lite, and the three rhythm voices are CC BY-SA and have to carry
// their attribution wherever they are redistributed.

#include "voicedata.h"

namespace y8 {
namespace {

"""

body = table("kPresetVoices", voices, BASIC_ROWS, vnames) + "\n\n"
body += table("kRhythmVoices", rhythm, PACKED_ROWS, rnames) + "\n\n"
body += table("kPresetWaves", waves, BASIC_ROWS, [str(i) for i in range(16)]) + "\n"

tail = """
} // namespace

VoiceRecord presetVoice(int n) {
    VoiceRecord r{};
    std::memcpy(r.data(), kPresetVoices + n * kVoiceRecordSize, kVoiceRecordSize);
    return r;
}

PackedVoice rhythmVoice(int n) {
    PackedVoice r{};
    std::memcpy(r.data(), kRhythmVoices + n * kPackedVoiceSize, kPackedVoiceSize);
    return r;
}

VoiceRecord presetWave(int n) {
    VoiceRecord r{};
    std::memcpy(r.data(), kPresetWaves + n * kVoiceRecordSize, kVoiceRecordSize);
    return r;
}

} // namespace y8
"""

out = pathlib.Path(args.output)
out.write_text(hdr + body + tail, encoding="ascii", newline="\n")
print(f"wrote {out} ({len(voices)+len(rhythm)+len(waves)} bytes of data)")
print("voices:", ", ".join(vnames[:4]), "...")
print("rhythm:", rnames)
