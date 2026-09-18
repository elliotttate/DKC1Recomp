#!/usr/bin/env python3
"""Regenerate runner/dkc1_dixie_patch.inc from the Dixie Kong Country IPS.

The include is committed deliberately (user-directed exception to the
"never commit extracted assets" rule) so the mod needs no external patched
ROM: the variant synthesizes the modded image in memory from the user's
verified clean DKC1 ROM at startup. See docs/DIXIE_HD_MAC.md.
"""

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ips", required=True, type=Path,
                        help="path to Dixie Kong Country.ips")
    parser.add_argument("--out", required=True, type=Path,
                        help="output include path (runner/dkc1_dixie_patch.inc)")
    args = parser.parse_args()

    data = args.ips.read_bytes()
    if data[:5] != b"PATCH" or data[-3:] != b"EOF":
        raise ValueError("not an IPS patch (missing PATCH/EOF markers)")

    lines = [
        "/* Embedded Dixie Kong Country mod patch (IPS format), byte-exact copy",
        " * of the hack's public distribution patch. Committed deliberately so the",
        " * mod needs no external patched ROM: the variant synthesizes the modded",
        " * image in memory from the user's verified clean DKC1 ROM. See",
        " * docs/DIXIE_HD_MAC.md. Regenerate with:",
        " *   python scripts/generate_dixie_patch_include.py --ips <patch> --out <inc>",
        " */",
        "#ifndef DKC1_DIXIE_PATCH_INC",
        "#define DKC1_DIXIE_PATCH_INC",
        "",
        "static const unsigned char kDkc1DixiePatchIps[] = {",
    ]
    for i in range(0, len(data), 16):
        lines.append("  " + ",".join(str(b) for b in data[i:i + 16]) + ",")
    lines += [
        "};",
        "",
        "static const unsigned long kDkc1DixiePatchIpsSize =",
        "    sizeof kDkc1DixiePatchIps;",
        "",
        "#endif",
    ]
    args.out.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"wrote {args.out}: {len(data)} patch bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
