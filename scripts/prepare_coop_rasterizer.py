"""Build the private co-op rasterizer from the pinned, read-only engine.

Host OBJ evaluation/palette lookup and admitted signed flashlight bounds differ.
Other backgrounds, windows, priority,
color math, and brightness retain the engine implementation. The ordinary PPU
translation unit is never changed.
"""
from pathlib import Path


def adapt(source: str) -> str:
    old = "ppu_evaluateSprites(ppu, line - 1)"
    if source.count(old) != 2:
        raise ValueError("private OBJ evaluator: pinned engine contract changed")
    source = source.replace(old, "Dkc1CoopViewEvaluateSprites(ppu, line - 1)")
    marker="  bool w1_ena = (winflags & kWindow1Enabled) && w1l <= w1r;"
    if source.count(marker)!=1: raise ValueError("private window contract changed")
    source=source.replace(marker,"  Dkc1CoopViewWindow(ppu,y,win->edges[0],window_right,&w1l,&w1r);\n"+marker)
    start = source.index("static NOINLINE void PpuDrawWholeLine(")
    end = source.index("static bool PpuWidescreenHudOamSlot(", start)
    body = source[start:end]
    old = "ppu->cgram[pixel & 0xff]"
    if body.count(old) != 2:
        raise ValueError("private main palette: pinned engine contract changed")
    body = body.replace(old, "Dkc1CoopViewColor(ppu, pixel, i, y - 1)")
    old = "ppu->cgram[ppu->bgBuffers[1].data[i] & 0xff]"
    if body.count(old) != 1:
        raise ValueError("private sub palette: pinned engine contract changed")
    body = body.replace(old,
        "Dkc1CoopViewColor(ppu, ppu->bgBuffers[1].data[i], i, y - 1)")
    return source[:start] + body + source[end:]


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    source = root / "snesrecomp/runner/src/snes/ppu.c"
    target = root / "generated/host/dkc1_coop_ppu.inc"
    result = adapt(source.read_text(encoding="utf-8"))
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text(encoding="utf-8") != result:
        target.write_text(result, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
