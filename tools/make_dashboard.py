#!/usr/bin/env python3
"""Generate the central regression dashboard (docs/DASHBOARD.md).

Collects, without running anything itself:
  - build identity (git commit/dirty state);
  - contract inventory and the latest run_regression --json-out results;
  - the latest level_sweep --json-out report (per-route flags);
  - the known-issue registry (docs/KNOWN_ISSUES.json);
  - evidence directory links for every entry.

Run it after a regression/sweep cycle so "did we lose an old fix while
solving a different scene" is a one-file answer, not an archaeology dig.
"""
from __future__ import annotations

import argparse
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def git(*args: str) -> str:
    try:
        return subprocess.run(["git", *args], cwd=REPO, text=True,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def load_json(path: Path):
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--regression-results", type=Path,
                        default=REPO / "build/regression/results.json")
    parser.add_argument("--sweep-report", type=Path,
                        default=REPO / "build/sweep/report.json")
    parser.add_argument("--out", type=Path,
                        default=REPO / "docs/DASHBOARD.md")
    parser.add_argument("--html", type=Path,
                        default=REPO / "docs/dashboard.html",
                        help="also render a styled single-file HTML report")
    args = parser.parse_args()

    commit = git("rev-parse", "--short", "HEAD")
    dirty = git("status", "--porcelain") != ""
    lines: list[str] = []
    out = lines.append
    out("# DKC1Recomp regression dashboard")
    out("")
    out(f"Generated {datetime.now(timezone.utc):%Y-%m-%d %H:%M} UTC at "
        f"commit `{commit}{'-dirty' if dirty else ''}`. Regenerate with "
        f"`python tools/make_dashboard.py` after a regression/sweep cycle.")
    out("")

    out("## Contracts")
    out("")
    results = (load_json(args.regression_results) or {}).get("results", {})
    out("| contract | last result | legs | evidence |")
    out("|---|---|---|---|")
    for contract_path in sorted((REPO / "contracts").glob("*.json")):
        spec = load_json(contract_path) or {}
        name = spec.get("name", contract_path.stem)
        if "checkpoints" not in spec:
            continue  # profiles etc., not runnable contracts
        entry = results.get(name)
        if entry is None:
            state = "NOT RUN in latest cycle"
            legs = evidence = "-"
        else:
            state = "PASS" if entry.get("passed") else \
                "**FAIL** (" + "; ".join(entry.get("failures", [])[:2]) + ")"
            legs = "+".join(entry.get("legs", []))
            evidence = f"`{entry.get('evidence', '-')}`"
        out(f"| {name} (`{contract_path.name}`) | {state} | {legs} "
            f"| {evidence} |")
    out("")

    out("## Route sweep")
    out("")
    sweep = load_json(args.sweep_report)
    if sweep is None:
        out("_No sweep report; run `python tools/level_sweep.py`._")
    else:
        out(f"_{sweep.get('coverage_note', '')}_")
        out("")
        out("| route | rc | cache oob (r/w) | rebases | oam wrap | "
            "scene flags |")
        out("|---|---|---|---|---|---|")
        for route, entry in sorted(sweep.get("routes", {}).items()):
            if "skipped" in entry:
                out(f"| {route} | skipped | - | - | - | "
                    f"{entry['skipped']} |")
                continue
            cache = entry.get("cache", {})
            wrap = entry.get("oam_wrap", {})
            flags = []
            for scene, stats in entry.get("scenes", {}).items():
                scene_flags = []
                if stats.get("raw_fallbacks"):
                    scene_flags.append("RAW")
                if stats.get("blank_serves"):
                    scene_flags.append(f"BLANK({stats['blank_serves']})")
                if stats.get("unstable_margin_frames"):
                    scene_flags.append("UNSTABLE")
                if stats.get("centered_in_gameplay"):
                    scene_flags.append("PILLARBOX")
                if scene_flags:
                    flags.append(f"{scene}: {','.join(scene_flags)}")
            out(f"| {route} | {entry.get('exit_code')} "
                f"| {cache.get('oob_read', '-')}/"
                f"{cache.get('oob_write', '-')} "
                f"| {cache.get('rebase', '-')} "
                f"| {wrap.get('wrap_suspects', '-')} "
                f"| {'; '.join(flags) or 'clean'} |")
    out("")

    out("## Known issues")
    out("")
    issues = (load_json(REPO / "docs/KNOWN_ISSUES.json") or {})
    out("| id | status | summary | repro |")
    out("|---|---|---|---|")
    for issue in issues.get("issues", []):
        out(f"| {issue.get('id')} | {issue.get('status')} "
            f"| {issue.get('summary')} | `{issue.get('repro', '-')}` |")
    out("")
    out("Issue lifecycle: edit `docs/KNOWN_ISSUES.json` (set status "
        "`fixed` with the fixing commit) and regenerate. A fixed issue "
        "regressing shows up here as its contract/sweep line failing.")
    out("")

    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {args.out}")
    if args.html:
        write_html(args.html, "\n".join(lines))
        print(f"wrote {args.html}")
    return 0


def write_html(out: Path, markdown_text: str) -> None:
    """Single-file HTML report from the markdown dashboard. Tables become
    real tables with pass/fail coloring; everything else is preformatted.
    No external assets, so the file can be attached to an issue as-is."""
    import html as html_module

    def render_table(rows):
        parts = ["<table>"]
        for i, row in enumerate(rows):
            cells = [c.strip() for c in row.strip().strip("|").split("|")]
            tag = "th" if i == 0 else "td"
            rendered = []
            for cell in cells:
                text_cell = html_module.escape(cell)
                css = ""
                if "FAIL" in cell or "EXCEED" in cell:
                    css = ' class="bad"'
                elif cell.startswith("PASS") or cell == "clean":
                    css = ' class="good"'
                elif "open" == cell:
                    css = ' class="open"'
                elif "fixed" == cell:
                    css = ' class="good"'
                rendered.append(f"<{tag}{css}>{text_cell}</{tag}>")
            parts.append("<tr>" + "".join(rendered) + "</tr>")
        parts.append("</table>")
        return "".join(parts)

    body = []
    table: list[str] = []
    for line in markdown_text.splitlines():
        stripped = line.strip()
        is_row = stripped.startswith("|")
        if table and not is_row:
            body.append(render_table([r for r in table
                                      if not set(r) <= set("|- ")]))
            table = []
        if is_row:
            if not set(stripped) <= set("|- "):
                table.append(stripped)
            continue
        if stripped.startswith("# "):
            body.append(f"<h1>{html_module.escape(stripped[2:])}</h1>")
        elif stripped.startswith("## "):
            body.append(f"<h2>{html_module.escape(stripped[3:])}</h2>")
        elif stripped.startswith("_") and stripped.endswith("_"):
            body.append(
                f"<p class='note'>{html_module.escape(stripped[1:-1])}</p>")
        elif stripped:
            body.append(f"<p>{html_module.escape(stripped)}</p>")
    if table:
        body.append(render_table([r for r in table
                                  if not set(r) <= set("|- ")]))

    out.write_text(
        "<!-- generated by make_dashboard.py --html -->\n"
        "<meta charset='utf-8'><title>DKC1Recomp dashboard</title><style>"
        "body{background:#121519;color:#dee6ee;"
        "font:14px/1.5 system-ui,sans-serif;max-width:1100px;"
        "margin:24px auto;padding:0 16px}"
        "h1,h2{color:#fff}code{color:#8fb8ff}"
        "table{border-collapse:collapse;width:100%;margin:12px 0;"
        "font-size:13px}"
        "th,td{border:1px solid #2a2f36;padding:6px 9px;text-align:left;"
        "vertical-align:top}"
        "th{background:#1a1e24}"
        ".good{color:#3fb96a;font-weight:600}"
        ".bad{color:#e5484d;font-weight:600}"
        ".open{color:#e8b339;font-weight:600}"
        ".note{color:#9aa4b0;font-style:italic}"
        "</style>" + "\n".join(body), encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
