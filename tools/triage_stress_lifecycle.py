#!/usr/bin/env python3
"""Rank authored-object lifecycle differences in a fresh-entry stress report.

The native and widescreen runtimes may place the same authored object in
different actor-pool slots.  Slot equality is therefore never used here.
Actors are segmented by gameplay context, grouped by their source-record
backlink, and their allocation episodes are aligned monotonically by identity,
world position, and time.  The output retains every finding (no truncation)
and separates presentation-neutral prefetch evidence from likely gameplay
regressions such as stock-only objects, early wide culls, bookmark drift, and
type-9 section divergence.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Any, Iterable


SCHEMA = "dkc1.stress-lifecycle-triage.v1"
ACTOR_FIELDS = ("id", "x", "y", "xs", "ys", "state", "anim", "pose")
SEVERITY_ORDER = {"critical": 4, "high": 3, "medium": 2, "low": 1,
                  "info": 0}


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(row, dict) and row.get("event"):
            rows.append(row)
    return rows


@dataclass
class Episode:
    source: int
    start: int
    alloc: dict[str, Any]
    end: int | None = None
    states: list[dict[str, Any]] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.states.append(self.alloc)

    def summary(self) -> dict[str, Any]:
        return {
            "start": self.start,
            "end": self.end,
            "actor_index": self.alloc.get("actor_index"),
            **{name: self.alloc.get(name) for name in ACTOR_FIELDS},
        }


@dataclass
class Segment:
    entrance: int
    ordinal: int
    start: int
    end: int | None = None
    exited: bool = False
    events: list[dict[str, Any]] = field(default_factory=list)
    episodes: dict[int, list[Episode]] = field(
        default_factory=lambda: defaultdict(list))
    bookmarks: dict[int, int] = field(default_factory=dict)
    bookmark_events: list[dict[str, Any]] = field(default_factory=list)
    sections: list[dict[str, Any]] = field(default_factory=list)


def segment_events(rows: Iterable[dict[str, Any]]) -> list[Segment]:
    segments: list[Segment] = []
    current: Segment | None = None
    per_entrance: Counter[int] = Counter()
    for row in rows:
        if row.get("schema") != "dkc1.lifecycle.v1":
            continue
        kind = row.get("event")
        if kind == "gameplay_enter":
            if current is not None:
                current.end = row.get("frame")
            entrance = int(row.get("entrance", -1))
            current = Segment(entrance, per_entrance[entrance],
                              int(row.get("frame", 0)))
            per_entrance[entrance] += 1
            segments.append(current)
        elif current is not None:
            current.events.append(row)
            if kind == "gameplay_exit":
                current.end = int(row.get("frame", current.start))
                current.exited = True
                current = None
    for segment in segments:
        _populate_segment(segment)
    return segments


def _populate_segment(segment: Segment) -> None:
    open_by_index: dict[int, Episode] = {}
    last_frame = segment.start
    for event in segment.events:
        kind = str(event.get("event"))
        frame = int(event.get("frame", last_frame))
        last_frame = max(last_frame, frame)
        if kind in ("actor_alloc", "slot_alloc", "actor_retype",
                    "slot_retype"):
            index = event.get("actor_index", event.get("slot"))
            source = event.get("source")
            if index is None or source is None:
                continue
            old = open_by_index.pop(int(index), None)
            if old is not None:
                old.end = frame
            source = int(source)
            if 0 < source < 0x100:
                episode = Episode(source, frame, event)
                open_by_index[int(index)] = episode
                segment.episodes[source].append(episode)
        elif kind in ("actor_state", "slot_state", "actor_sample"):
            index = event.get("actor_index", event.get("slot"))
            episode = open_by_index.get(int(index)) if index is not None else None
            if episode is not None:
                event_source = event.get("source")
                if event_source is None or int(event_source) == episode.source:
                    episode.states.append(event)
        elif kind in ("actor_free", "slot_free"):
            index = event.get("actor_index", event.get("slot"))
            episode = open_by_index.pop(int(index), None) \
                if index is not None else None
            if episode is not None:
                episode.end = frame
        elif kind == "bookmark":
            record = int(event.get("record", -1))
            if 0 < record < 0x100:
                segment.bookmarks[record] = int(event.get("to", 0))
                segment.bookmark_events.append(event)
        elif kind == "section":
            segment.sections.append({
                key: event.get(key)
                for key in ("frame", "state", "pointer", "current",
                            "pending", "limit")
            })
    observed_end = segment.end if segment.end is not None else last_frame
    for episode in open_by_index.values():
        episode.end = None  # explicit: still live at the observation horizon
    if segment.end is None:
        segment.end = observed_end


def circular_distance(a: Any, b: Any) -> int:
    if not isinstance(a, int) or not isinstance(b, int):
        return 0 if a == b else 0x1000
    delta = abs((a & 0xFFFF) - (b & 0xFFFF))
    return min(delta, 0x10000 - delta)


def match_cost(stock: Episode, wide: Episode) -> float:
    id_cost = 0.0 if stock.alloc.get("id") == wide.alloc.get("id") else 320.0
    position = (circular_distance(stock.alloc.get("x"), wide.alloc.get("x")) +
                circular_distance(stock.alloc.get("y"), wide.alloc.get("y")))
    time = abs(stock.start - wide.start)
    return id_cost + min(position / 8.0, 240.0) + min(time / 30.0, 80.0)


def align_episodes(stock: list[Episode], wide: list[Episode]) \
        -> list[tuple[Episode | None, Episode | None]]:
    """Needleman-Wunsch alignment; source equality is guaranteed by caller."""
    n, m = len(stock), len(wide)
    gap = 110.0
    scores = [[0.0] * (m + 1) for _ in range(n + 1)]
    moves = [[""] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        scores[i][0] = i * gap
        moves[i][0] = "stock"
    for j in range(1, m + 1):
        scores[0][j] = j * gap
        moves[0][j] = "wide"
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            candidates = (
                (scores[i - 1][j - 1] + match_cost(stock[i - 1], wide[j - 1]),
                 "match"),
                (scores[i - 1][j] + gap, "stock"),
                (scores[i][j - 1] + gap, "wide"),
            )
            scores[i][j], moves[i][j] = min(candidates, key=lambda item: item[0])
    result: list[tuple[Episode | None, Episode | None]] = []
    i, j = n, m
    while i or j:
        move = moves[i][j]
        if move == "match":
            result.append((stock[i - 1], wide[j - 1]))
            i -= 1
            j -= 1
        elif move == "stock":
            result.append((stock[i - 1], None))
            i -= 1
        else:
            result.append((None, wide[j - 1]))
            j -= 1
    result.reverse()
    return result


def field_differences(stock: Episode, wide: Episode) -> dict[str, Any]:
    return {
        name: {"native": stock.alloc.get(name), "wide": wide.alloc.get(name)}
        for name in ACTOR_FIELDS
        if stock.alloc.get(name) != wide.alloc.get(name)
    }


def make_finding(verdict: str, severity: str, source: int,
                 stock: Episode | None, wide: Episode | None,
                 **extra: Any) -> dict[str, Any]:
    result: dict[str, Any] = {
        "verdict": verdict, "severity": severity, "source": source,
        "native": stock.summary() if stock else None,
        "wide": wide.summary() if wide else None,
    }
    result.update(extra)
    return result


def classify_pair(source: int, stock: Episode | None,
                  wide: Episode | None) -> dict[str, Any]:
    if stock is None:
        return make_finding("wide_only", "medium", source, stock, wide,
                            disposition="indeterminate")
    if wide is None:
        return make_finding("stock_only", "critical", source, stock, wide)
    differences = field_differences(stock, wide)
    lead = stock.start - wide.start
    stock_end = stock.end
    wide_end = wide.end
    if stock_end is not None and (wide_end is None or wide_end > stock_end + 2):
        return make_finding("wide_persists_stock_culls", "medium", source,
                            stock, wide, disposition="indeterminate",
                            lead_frames=lead, alloc_differences=differences)
    if wide_end is not None and (stock_end is None or stock_end > wide_end + 2):
        return make_finding("stock_persists_wide_culls", "high", source,
                            stock, wide, lead_frames=lead,
                            alloc_differences=differences)
    if lead < 0:
        return make_finding("wide_allocates_late", "high", source, stock, wide,
                            lead_frames=lead, alloc_differences=differences)
    if lead > 0:
        return make_finding("prefetch_needs_exact_sample", "medium", source,
                            stock, wide, disposition="indeterminate",
                            lead_frames=lead, alloc_differences=differences)
    if differences:
        return make_finding("allocation_state_difference", "high", source,
                            stock, wide, alloc_differences=differences)
    return make_finding("matched", "info", source, stock, wide)


def covering_episode(target: Episode, candidates: list[Episode]) \
        -> Episode | None:
    """Return a same-object episode that spans target's allocation frame.

    A wider scanner can keep one actor allocated while stock culls and later
    reallocates the same source record.  One-to-one sequence alignment must
    leave one of those stock episodes unmatched, but that is not a missing
    wide object.  Match by authored source (the caller's group), identity,
    allocation world position, and lifetime coverage—never allocator slot.
    """
    for candidate in candidates:
        if candidate.alloc.get("id") != target.alloc.get("id"):
            continue
        if (circular_distance(candidate.alloc.get("x"), target.alloc.get("x")) +
                circular_distance(candidate.alloc.get("y"),
                                  target.alloc.get("y"))) > 16:
            continue
        if candidate.start > target.start:
            continue
        if candidate.end is not None and candidate.end < target.start:
            continue
        return candidate
    return None


def section_signature(segment: Segment) -> list[tuple[Any, ...]]:
    keys = ("state", "pointer", "current", "pending", "limit")
    result: list[tuple[Any, ...]] = []
    for row in segment.sections:
        # The generic scanner writes limit=$FFFF while all type-9 section
        # fields are zero.  That transition is not a section-controller
        # lifecycle and must not rank as a Slipslide-style divergence.
        if not any(int(row.get(key) or 0)
                   for key in ("state", "pointer", "current", "pending")):
            continue
        value = tuple(row.get(key) for key in keys)
        if not result or value != result[-1]:
            result.append(value)
    return result


def compare_segments(stock: Segment, wide: Segment) -> list[dict[str, Any]]:
    findings: list[dict[str, Any]] = []
    for source in sorted(set(stock.episodes) | set(wide.episodes)):
        stock_episodes = stock.episodes.get(source, [])
        wide_episodes = wide.episodes.get(source, [])
        for native_ep, wide_ep in align_episodes(stock_episodes, wide_episodes):
            if native_ep is not None and wide_ep is None:
                covering = covering_episode(native_ep, wide_episodes)
                if covering is not None:
                    findings.append(make_finding(
                        "wide_persists_stock_culls", "medium", source,
                        native_ep, covering, disposition="indeterminate",
                        reallocation_covered=True,
                        lead_frames=native_ep.start - covering.start,
                        alloc_differences=field_differences(native_ep,
                                                            covering)))
                    continue
            if wide_ep is not None and native_ep is None:
                covering = covering_episode(wide_ep, stock_episodes)
                if covering is not None:
                    findings.append(make_finding(
                        "stock_persists_wide_culls", "high", source,
                        covering, wide_ep, reallocation_covered=True,
                        lead_frames=covering.start - wide_ep.start,
                        alloc_differences=field_differences(covering,
                                                            wide_ep)))
                    continue
            findings.append(classify_pair(source, native_ep, wide_ep))
    records = sorted(set(stock.bookmarks) | set(wide.bookmarks))
    for record in records:
        native_value = stock.bookmarks.get(record, 0)
        wide_value = wide.bookmarks.get(record, 0)
        if native_value != wide_value:
            findings.append({
                # Wider prefetch intentionally changes the slot value stored
                # in $192B.  Retain it as evidence, but only actor/section or
                # outcome evidence can promote it to a gameplay failure.
                "verdict": "bookmark_final_difference", "severity": "medium",
                "source": record, "native": native_value, "wide": wide_value,
            })
    native_sections = section_signature(stock)
    wide_sections = section_signature(wide)
    if native_sections != wide_sections:
        findings.append({
            "verdict": "section_sequence_difference", "severity": "critical",
            "source": None, "native": native_sections, "wide": wide_sections,
        })
    if stock.exited != wide.exited:
        findings.append({
            "verdict": "gameplay_exit_difference", "severity": "critical",
            "source": None,
            "native": {"exited": stock.exited, "frame": stock.end},
            "wide": {"exited": wide.exited, "frame": wide.end},
        })
    return findings


def pair_segments(stock: list[Segment], wide: list[Segment]) \
        -> list[tuple[Segment | None, Segment | None]]:
    stock_map = {(item.entrance, item.ordinal): item for item in stock}
    wide_map = {(item.entrance, item.ordinal): item for item in wide}
    keys = sorted(set(stock_map) | set(wide_map))
    return [(stock_map.get(key), wide_map.get(key)) for key in keys]


def branch_triage(branch: dict[str, Any]) -> dict[str, Any]:
    native_runs = branch.get("native_runs") or []
    wide_runs = branch.get("wide_runs") or []
    if not native_runs or not wide_runs:
        return {
            "entrance": branch.get("entrance"), "name": branch.get("name"),
            "action": branch.get("action"), "mask": branch.get("mask"),
            "native_lifecycle": None, "wide_lifecycle": None,
            "segment_counts": {"native": 0, "wide": 0},
            "max_severity": "info", "findings": [],
            "skipped": True,
            "skip_status": branch.get("status"),
            "skip_failures": branch.get("failures", []),
            "skip_investigations": branch.get("investigations", []),
        }
    native_path = Path(native_runs[0]["artifacts"]["lifecycle"])
    wide_path = Path(wide_runs[0]["artifacts"]["lifecycle"])
    native_segments = segment_events(load_jsonl(native_path))
    wide_segments = segment_events(load_jsonl(wide_path))
    findings: list[dict[str, Any]] = []
    for native_segment, wide_segment in pair_segments(native_segments, wide_segments):
        if native_segment is None or wide_segment is None:
            findings.append({
                "verdict": "gameplay_context_missing", "severity": "critical",
                "source": None,
                "native": None if native_segment is None else {
                    "entrance": native_segment.entrance,
                    "ordinal": native_segment.ordinal,
                },
                "wide": None if wide_segment is None else {
                    "entrance": wide_segment.entrance,
                    "ordinal": wide_segment.ordinal,
                },
            })
            continue
        for finding in compare_segments(native_segment, wide_segment):
            finding["context"] = {
                "entrance": native_segment.entrance,
                "ordinal": native_segment.ordinal,
            }
            findings.append(finding)
    actionable = [row for row in findings if row["verdict"] != "matched"]
    max_severity = max((SEVERITY_ORDER[row["severity"]] for row in actionable),
                       default=0)
    return {
        "entrance": branch.get("entrance"), "name": branch.get("name"),
        "action": branch.get("action"), "mask": branch.get("mask"),
        "native_lifecycle": str(native_path), "wide_lifecycle": str(wide_path),
        "segment_counts": {"native": len(native_segments), "wide": len(wide_segments)},
        "max_severity": next((name for name, value in SEVERITY_ORDER.items()
                              if value == max_severity), "info"),
        "findings": findings,
    }


def triage_report(report: dict[str, Any], source_path: Path) -> dict[str, Any]:
    branches = [branch_triage(branch) for branch in report.get("branches", [])]
    verdicts: Counter[str] = Counter()
    severities: Counter[str] = Counter()
    for branch in branches:
        for finding in branch["findings"]:
            verdicts[finding["verdict"]] += 1
            severities[finding["severity"]] += 1
    branches.sort(key=lambda row: (
        -SEVERITY_ORDER[row["max_severity"]],
        -sum(1 for finding in row["findings"]
             if finding["verdict"] != "matched"),
        int(row.get("entrance") or -1), str(row.get("action"))),
    )
    return {
        "schema": SCHEMA,
        "source_report": str(source_path.resolve()),
        "summary": {
            "branches": len(branches),
            "branches_with_findings": sum(
                any(f["verdict"] != "matched" for f in row["findings"])
                for row in branches),
            "verdicts": dict(sorted(verdicts.items())),
            "severities": dict(sorted(severities.items())),
        },
        "branches": branches,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--json-out", type=Path, required=True)
    args = parser.parse_args()
    source = json.loads(args.report.read_text(encoding="utf-8"))
    result = triage_report(source, args.report)
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(result, indent=2),
                             encoding="utf-8", newline="\n")
    print(json.dumps(result["summary"], indent=2))
    for branch in result["branches"][:12]:
        actionable = [item for item in branch["findings"]
                      if item["verdict"] != "matched"]
        if actionable:
            labels = Counter(item["verdict"] for item in actionable)
            print(f"{branch['max_severity']:8s} {branch['name']} "
                  f"{branch['action']}: {dict(labels)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
