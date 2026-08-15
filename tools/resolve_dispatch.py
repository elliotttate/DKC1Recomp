#!/usr/bin/env python3
"""Turn observed indirect-dispatch targets into a cfg contract line.

Workflow for a RAM-pointer dispatch the analyzer reports as unresolved
(e.g. `$BE8179`, the animation callback `JML [$007A]` behind a
`PHK / PEA $810D` return frame):

  1. temporarily add `force_lle <containing_function_pc24>` to the bank cfg
     and regenerate, so the site executes on the interpreter tier;
  2. run routes with DKC1_TRACE_PC=<site> — the headless TracePc line
     includes `ptr7a=$XXXXXX`, the live [$007A] target;
  3. feed those stderr logs to this tool: it collects the distinct targets
     and prints the `indirect_dispatch` contract line (ptrcall for
     PEA-return sites, ptrtail for plain JML), plus coverage notes;
  4. replace force_lle with the printed contract, regenerate, re-run the
     route, and confirm the unresolved-abandon report is gone.

Route coverage matters: the emitted contract only contains targets the
routes exercised. Keep force_lle harvesting until new targets stop
appearing, and treat a runtime pointer_match miss as a signal to re-run
this workflow, not to widen blindly.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TRACE_RE = re.compile(
    r"dkc1_trace_pc .*pc=\$(?P<pc>[0-9a-f]{6}).*ptr7a=\$(?P<ptr>[0-9a-f]{6})",
    re.IGNORECASE)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path,
                        help="stderr logs from DKC1_TRACE_PC runs")
    parser.add_argument("--site", required=True,
                        help="24-bit dispatch site, e.g. BE8179")
    parser.add_argument("--mode", choices=("ptrcall", "ptrtail"),
                        default="ptrcall")
    parser.add_argument("--return-pc",
                        help="16-bit continuation for ptrcall (PEA operand)")
    parser.add_argument("--frame", choices=("2", "3"), default="3",
                        help="return frame size: 2=RTS, 3=RTL")
    args = parser.parse_args()

    site = int(args.site, 16) & 0xFFFFFF
    targets: dict[int, int] = {}
    lines = 0
    for path in args.logs:
        for line in path.read_text(errors="replace").splitlines():
            match = TRACE_RE.search(line)
            if not match:
                continue
            if int(match.group("pc"), 16) != site:
                continue
            lines += 1
            pointer = int(match.group("ptr"), 16)
            targets[pointer] = targets.get(pointer, 0) + 1

    if not targets:
        print("no targets observed — is force_lle active and "
              "DKC1_TRACE_PC set to the site?", file=sys.stderr)
        return 1

    ordered = sorted(targets)
    print(f"site ${site:06X}: {lines} executions, "
          f"{len(ordered)} distinct targets")
    for value in ordered:
        print(f"  ${value:06X}  x{targets[value]}")

    rendered = ",".join(f"{value:06X}" for value in ordered)
    options = args.mode
    if args.mode == "ptrcall":
        if args.return_pc:
            options += f" return:{int(args.return_pc, 16) & 0xFFFF:04X}"
        options += f" frame:{args.frame}"
    print("\ncontract line for the bank cfg:")
    print(f"indirect_dispatch {site & 0xFFFF:04X} {len(ordered)} "
          f"{options} targets:{rendered}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
