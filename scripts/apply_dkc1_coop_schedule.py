"""Atomic host execution at the four cartridge placed-actor JSL sites.

The same byte-grounded call/return inventory prepares the interpreter hooks.
Only host continuation handles take the fast path; a rejected transaction
falls through the untouched timed call, at the same actor-loop position.
"""
from pathlib import Path
import importlib.util
import re

_spec = importlib.util.spec_from_file_location(
    "coop_schedule_entities", Path(__file__).with_name("apply_dkc1_coop_entities.py"))
_entities = importlib.util.module_from_spec(_spec)
assert _spec.loader is not None
_spec.loader.exec_module(_entities)
_function = _entities._function

# owner, JSL PC, target, return PC, existing co-op Begin/End overlay
CALLS = (
    ("CODE_BF8000_M0X0", 0xBF802F, 0xBF8087, 0xBF8033, True),
    ("CODE_BF804B_M0X0", 0xBF8069, 0xBF8087, 0xBF806D, True),
    ("CODE_BF8091_M0X0", 0xBF80B1, 0xBF8087, 0xBF80B5, False),
    ("CODE_BF8091_M0X0", 0xBF80E0, 0xBF808A, 0xBF80E4, False),
)


def apply_overrides(sources: dict[Path, str]) -> dict[str, int]:
    changed = 0
    anchor = "    {\n      /* JSL return frame -> cpu->S (Option-1) */\n"
    for owner, call, target, ret, overlay in CALLS:
        path, start, end = _function(sources, owner)
        text = sources[path]
        body = text[start:end]
        markers = list(re.finditer(
            rf"cpu_trace_pb_change\(cpu, 0x{call:06x}u, _saved_pb,", body))
        if len(markers) != 1:
            raise ValueError(f"expected one actor JSL ${call:06X}")
        at = body.rfind(anchor, 0, markers[0].start())
        if at < 0:
            raise ValueError(f"missing actor call frame ${call:06X}")
        label = f"L_{ret & 0xffff:04X}_M0X0"
        if f"  {label}:" not in body:
            raise ValueError(f"missing actor return ${ret:06X}")
        hook = f"Dkc1CoopScheduleActor(cpu, 0x{target:06x}u, 0x{ret:06x}u)"
        if hook in body:
            if body.count(hook) != 1:
                raise ValueError(f"duplicate scheduling hook ${call:06X}")
            continue
        insertion = f"    if ({hook}) {{\n"
        if overlay:
            insertion += "      Dkc1VideoEndPlacedActorDispatch(cpu);\n"
        insertion += f"      goto {label};\n    }}\n"
        body = body[:at] + insertion + body[at:]
        text = text[:start] + body + text[end:]
        include = '#include "dkc1_coop_schedule.h"\n'
        if include not in text:
            text = include + text
        sources[path] = text
        changed += 1
    return {"coop_schedule": changed}
