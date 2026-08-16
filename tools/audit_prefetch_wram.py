#!/usr/bin/env python3
"""Audit stock-vs-wide object phase directly from per-frame WRAM dumps.

This is the raw-byte companion to ``audit_prefetch_phases.py``.  It consumes
the sparse or full ``wram_dump.c`` prefixes produced by first_divergence.py,
matches actors by authored source record (never pool slot), constructs every
allocation/cull/reallocation episode, and compares the wide actor at the
exact frame stock first allocates the same record.

Verdicts are deliberately conservative:

* ``harmless_visual_prefetch`` means all captured actor words match at the
  stock allocation frame.
* ``animation_phase_advancement`` means only draw/animation fields differ.
* ``behavior_phase_advancement`` means a named position, motion, state,
  identity, or flags field differs.
* ``indeterminate_actor_work_difference`` means only unnamed actor scratch
  words differ.  Those words may matter for one actor family, but a changed
  pool slot can also contain harmless residue, so this tool does not promote
  them to a behavioral claim without a semantic trace.
* wide-only and persistence cases remain indeterminate.

The input dump must include $0AE5-$1698 (all 26 normal-actor array words).
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from first_divergence import load_wram_frames, read16  # noqa: E402


ACTOR_FIRST = 0x02
ACTOR_LAST = 0x32
ACTOR_ARRAYS = {
    "displayed_pose": 0x0AE5,
    "x": 0x0B19,
    "oam_z": 0x0B8D,
    "y": 0x0BC1,
    "work_0c35": 0x0C35,
    "gfx": 0x0C69,
    "work_0cdd": 0x0CDD,
    "current_pose": 0x0D11,
    "id": 0x0D45,
    "work_0db9": 0x0DB9,
    "work_0ded": 0x0DED,
    "work_0e21": 0x0E21,
    "work_0e55": 0x0E55,
    "x_speed": 0x0E89,
    "work_0ebd": 0x0EBD,
    "y_speed": 0x0EF1,
    "work_0f25": 0x0F25,
    "work_0f59": 0x0F59,
    "work_0f8d": 0x0F8D,
    "work_0fc1": 0x0FC1,
    "work_0ff5": 0x0FF5,
    "state": 0x1029,
    "work_109d": 0x109D,
    "animation": 0x10D1,
    "pose_timer": 0x1105,
    "animation_speed": 0x1139,
    "animation_index": 0x116D,
    "work_11a1": 0x11A1,
    "work_11d5": 0x11D5,
    "work_1209": 0x1209,
    "work_123d": 0x123D,
    "work_1271": 0x1271,
    "flags": 0x12A5,
    "work_12d9": 0x12D9,
    "work_130d": 0x130D,
    "work_1341": 0x1341,
    "work_1375": 0x1375,
    "work_13e9": 0x13E9,
    "work_145d": 0x145D,
    "work_1491": 0x1491,
    "work_14c5": 0x14C5,
    "work_14f9": 0x14F9,
    "work_152d": 0x152D,
    "work_1561": 0x1561,
    "work_1595": 0x1595,
    "work_15c9": 0x15C9,
    "source": 0x15FD,
    "work_1631": 0x1631,
    "work_1665": 0x1665,
}

PRESENTATION_OR_ANIMATION_FIELDS = {
    "displayed_pose", "oam_z", "gfx", "current_pose", "animation",
    "pose_timer", "animation_speed", "animation_index",
}

BEHAVIOR_FIELDS = {
    "id", "x", "y", "x_speed", "y_speed", "state", "flags",
}

OPAQUE_WORK_FIELDS = {
    name for name in ACTOR_ARRAYS if name.startswith("work_")
}


def signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def actor_state(memory: bytes, index: int) -> dict:
    result = {name: read16(memory, base + index)
              for name, base in ACTOR_ARRAYS.items()}
    result["actor_index"] = index
    result["source_signed"] = signed16(result["source"])
    return result


def actors_by_source(memory: bytes) -> tuple[dict[int, dict], list[dict]]:
    grouped: dict[int, list[dict]] = defaultdict(list)
    for index in range(ACTOR_FIRST, ACTOR_LAST + 1, 2):
        state = actor_state(memory, index)
        source = state["source_signed"]
        # DK and Diddy occupy raw actor indexes $02/$04 and both carry a zero
        # source word.  That zero is not authored record 0 and must not be
        # reported as a duplicated placed object.  Preserve a genuine record
        # 0 actor in either slot when its ID is not a Kong ID.
        if index in (0x02, 0x04) and source == 0 and state["id"] in (1, 2):
            continue
        if state["id"] and 0 <= source < 0x100:
            grouped[source].append(state)
    duplicates = []
    selected = {}
    for source, states in grouped.items():
        states.sort(key=lambda item: item["actor_index"])
        selected[source] = states[0]
        if len(states) > 1:
            duplicates.append({
                "source": source,
                "actor_indices": [state["actor_index"] for state in states],
            })
    return selected, duplicates


@dataclass
class Episode:
    source: int
    start: int
    end: int | None = None
    samples: dict[int, dict] = field(default_factory=dict)
    actor_indices: set[int] = field(default_factory=set)

    def active_at(self, frame: int) -> bool:
        return self.start <= frame and (self.end is None or frame < self.end)


def build_episodes(frames: dict[int, bytes]) -> tuple[dict[int, list[Episode]],
                                                       list[dict]]:
    episodes: dict[int, list[Episode]] = defaultdict(list)
    active: dict[int, Episode] = {}
    duplicate_observations = []
    previous_frame = None
    for frame in sorted(frames):
        if previous_frame is not None and frame != previous_frame + 1:
            raise ValueError("WRAM phase audit requires consecutive frames")
        present, duplicates = actors_by_source(frames[frame])
        for duplicate in duplicates:
            duplicate_observations.append({"frame": frame, **duplicate})
        for source in list(active):
            if source not in present:
                active[source].end = frame
                del active[source]
        for source, state in present.items():
            episode = active.get(source)
            if episode is None or episode.samples[max(episode.samples)]["id"] != state["id"]:
                if episode is not None:
                    episode.end = frame
                episode = Episode(source=source, start=frame)
                active[source] = episode
                episodes[source].append(episode)
            episode.samples[frame] = state
            episode.actor_indices.add(state["actor_index"])
        previous_frame = frame
    return episodes, duplicate_observations


def compare_actor_states(stock: dict, wide: dict) -> dict:
    differences = {}
    for field_name in ACTOR_ARRAYS:
        if field_name == "source":
            continue
        if stock[field_name] != wide[field_name]:
            differences[field_name] = {
                "stock": f"0x{stock[field_name]:04X}",
                "wide": f"0x{wide[field_name]:04X}",
            }
    return differences


def split_differences(differences: dict) -> dict[str, dict]:
    """Separate proven semantic fields from render and unnamed scratch.

    DKC's structure-of-arrays slots are reused without a universal full clear.
    Comparing every word remains useful evidence, but an unnamed word alone
    cannot prove phase advancement when stock and wide used different slots.
    """
    return {
        "behavior": {
            name: value for name, value in differences.items()
            if name in BEHAVIOR_FIELDS
        },
        "animation_render": {
            name: value for name, value in differences.items()
            if name in PRESENTATION_OR_ANIMATION_FIELDS
        },
        "opaque_work": {
            name: value for name, value in differences.items()
            if name in OPAQUE_WORK_FIELDS
        },
    }


def episode_for_frame(episodes: list[Episode], frame: int) -> Episode | None:
    return next((episode for episode in episodes if episode.active_at(frame)),
                None)


def audit_frames(stock_frames: dict[int, bytes],
                 wide_frames: dict[int, bytes]) -> dict:
    common = sorted(set(stock_frames) & set(wide_frames))
    if not common:
        raise ValueError("stock and wide WRAM dumps have no common frames")
    if common != list(range(common[0], common[-1] + 1)):
        raise ValueError("stock/wide common frame range is not consecutive")

    stock_episodes, stock_duplicates = build_episodes(
        {frame: stock_frames[frame] for frame in common})
    wide_episodes, wide_duplicates = build_episodes(
        {frame: wide_frames[frame] for frame in common})

    findings = []
    all_sources = sorted(set(stock_episodes) | set(wide_episodes))
    for source in all_sources:
        stocks = stock_episodes.get(source, [])
        wides = wide_episodes.get(source, [])
        used_wide: set[int] = set()
        for ordinal, stock_episode in enumerate(stocks):
            wide_episode = episode_for_frame(wides, stock_episode.start)
            if wide_episode is None:
                later = next((episode for episode in wides
                              if episode.start > stock_episode.start), None)
                findings.append({
                    "source": source,
                    "stock_episode": ordinal,
                    "verdict": "wide_allocates_late" if later else "stock_only",
                    "stock_start": stock_episode.start,
                    "wide_start": None if later is None else later.start,
                    "disposition": "indeterminate",
                })
                continue
            wide_ordinal = wides.index(wide_episode)
            used_wide.add(wide_ordinal)
            stock_state = stock_episode.samples[stock_episode.start]
            wide_state = wide_episode.samples.get(stock_episode.start)
            if wide_state is None:
                raise ValueError("active wide episode lacks exact-frame sample")
            differences = compare_actor_states(stock_state, wide_state)
            groups = split_differences(differences)
            lead = stock_episode.start - wide_episode.start
            stock_end = stock_episode.end
            wide_end = wide_episode.end
            persists = (stock_end is not None and
                        (wide_end is None or wide_end > stock_end + 2))
            finding = {
                "source": source,
                "stock_episode": ordinal,
                "wide_episode": wide_ordinal,
                "id": f"0x{stock_state['id']:04X}",
                "stock_start": stock_episode.start,
                "wide_start": wide_episode.start,
                "wide_lead_frames": lead,
                "stock_actor_indices": sorted(stock_episode.actor_indices),
                "wide_actor_indices": sorted(wide_episode.actor_indices),
            }
            if persists:
                finding.update({
                    "persists_past_stock_cull": True,
                    "stock_end": stock_end,
                    "wide_end": wide_end,
                })
            if differences:
                finding["differences_at_stock_allocation"] = differences
                finding["difference_groups"] = groups

            if lead > 0 and groups["behavior"]:
                finding["verdict"] = "behavior_phase_advancement"
            elif lead > 0 and groups["opaque_work"]:
                finding["verdict"] = "indeterminate_actor_work_difference"
                finding["disposition"] = "requires_semantic_trace"
            elif persists:
                finding["verdict"] = "wide_persists_stock_culls"
                finding["disposition"] = "indeterminate"
            elif lead > 0 and not differences:
                finding["verdict"] = "harmless_visual_prefetch"
            elif lead > 0 and groups["animation_render"]:
                finding["verdict"] = "animation_phase_advancement"
                finding["disposition"] = "requires_visual_oracle"
            elif lead == 0 and not differences:
                finding["verdict"] = "matched"
            elif groups["behavior"]:
                finding["verdict"] = "behavior_phase_difference"
            elif groups["opaque_work"]:
                finding["verdict"] = "indeterminate_actor_work_difference"
                finding["disposition"] = "requires_semantic_trace"
            else:
                finding["verdict"] = "animation_phase_difference"
                finding["disposition"] = "requires_visual_oracle"
            findings.append(finding)

        for wide_ordinal, wide_episode in enumerate(wides):
            if wide_ordinal not in used_wide:
                findings.append({
                    "source": source,
                    "wide_episode": wide_ordinal,
                    "verdict": "indeterminate_without_stock_allocation",
                    "wide_start": wide_episode.start,
                    "wide_end": wide_episode.end,
                    "disposition": "indeterminate",
                })

    verdicts = Counter(finding["verdict"] for finding in findings)
    return {
        "schema": "dkc1.prefetch-wram-audit.v1",
        "frames": [common[0], common[-1]],
        "sources": len(all_sources),
        "episodes": len(findings),
        "verdicts": dict(sorted(verdicts.items())),
        "duplicate_source_observations": {
            "stock": stock_duplicates[:64],
            "wide": wide_duplicates[:64],
        },
        "findings": findings,
    }


def normalize_prefix(path: Path) -> Path:
    text = str(path)
    if text.endswith(".bin.jsonl"):
        text = text[:-10]
    elif text.endswith(".bin"):
        text = text[:-4]
    return Path(text)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock", type=Path,
                        help="stock WRAM dump prefix or .bin path")
    parser.add_argument("wide", type=Path,
                        help="wide WRAM dump prefix or .bin path")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    stock_frames = load_wram_frames(normalize_prefix(args.stock))
    wide_frames = load_wram_frames(normalize_prefix(args.wide))
    report = audit_frames(stock_frames, wide_frames)
    output = json.dumps(report, indent=1) + "\n"
    print(output, end="")
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
