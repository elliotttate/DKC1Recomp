#!/usr/bin/env python3
"""Mechanical syntax translation of the project's Metal shader passes to HLSL.

The Mac source (runner/macos_graphics.metal) remains authoritative. The
generated header holds only project-owned shader text, no game data. Every
fragment pass keeps its Metal name and becomes a pixel-shader entry point in
one HLSL source; the Windows Direct3D 11 presenter compiles the passes it
uses. The transformation is purely syntactic so the arithmetic stays
identical to the Metal and OpenGL paths:

  texture2d<float> -> Texture2D           .sample(s, uv) -> .Sample(s, uv)
  fract/mix        -> frac/lerp            in.uv / in.position -> input.*
  typeN(x)         -> ((typeN)(x))         single-argument splat/convert
  constant float *u -> cbuffer float u[27] (16-byte slots, CPU stride 16)
"""
import json
import re
import sys
from pathlib import Path

TYPE_CAST = re.compile(r"\b(float[234]?|uint2?|int)\(")
FRAGMENT = re.compile(r"fragment float4 (dkc1_\w+)\((.*?)\)\s*\{", re.S)
VERTEX = re.compile(r"vertex VertexOutput dkc1_vertex\(.*?\n\}\n", re.S)
RESERVED = {"pass": "pass_"}

HEADER = """struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
cbuffer Params : register(b0) { float u[27]; };
Texture2D source : register(t0);
Texture2D lines : register(t0);
Texture2D image : register(t0);
Texture2D glow : register(t1);
Texture2D halo : register(t2);
SamplerState pointSampler : register(s0);
SamplerState linearSampler : register(s1);
VSOut dkc1_vertex(uint id : SV_VertexID) {
  float2 positions[4] = {float2(-1, 1), float2(-1, -1), float2(1, 1), float2(1, -1)};
  float2 coords[4] = {float2(0, 0), float2(0, 1), float2(1, 0), float2(1, 1)};
  VSOut o; o.pos = float4(positions[id], 0, 1); o.uv = coords[id]; return o;
}
"""


def castify(source):
    """Rewrite single-argument constructors as casts; HLSL rejects float3(x)."""
    out, pos = [], 0
    while True:
        match = TYPE_CAST.search(source, pos)
        if not match:
            out.append(source[pos:])
            return "".join(out)
        depth, commas, i = 1, 0, match.end()
        while i < len(source) and depth:
            c = source[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == "," and depth == 1:
                commas += 1
            i += 1
        if depth:
            raise ValueError("unbalanced constructor call")
        inner = source[match.end():i - 1]
        out.append(source[pos:match.start()])
        if commas == 0:
            out.append(f"(({match.group(1)})({castify(inner)}))")
        else:
            out.append(f"{match.group(1)}({castify(inner)})")
        pos = i


def translate(source):
    body = source.split("using namespace metal;", 1)[1]
    body = re.sub(r"^struct VertexOutput .*$", "", body, flags=re.M)
    body = re.sub(r"^constexpr sampler .*$", "", body, flags=re.M)
    body = VERTEX.sub("", body)
    names = FRAGMENT.findall(body)
    if not names:
        raise ValueError("no Metal fragment passes found")
    body = FRAGMENT.sub(r"float4 \1(VSOut input) : SV_Target {", body)
    body = body.replace("texture2d<float>", "Texture2D")
    body = re.sub(r"\.sample\((pointSampler|linearSampler),", r".Sample(\1,", body)
    body = body.replace("in.uv", "input.uv").replace("in.position", "input.pos")
    body = re.sub(r"\bfract\(", "frac(", body)
    body = re.sub(r"\bmix\(", "lerp(", body)
    for word, replacement in RESERVED.items():
        body = re.sub(rf"\b{word}\b", replacement, body)
    body = castify(body)
    if "[[" in body or ".sample(" in body or "texture2d" in body:
        raise ValueError("untranslated Metal syntax remains")
    return HEADER + body, [name for name, _ in names]


def main(argv):
    hlsl, names = translate(Path(argv[1]).read_text(encoding="utf-8"))
    text = ("/* Generated from macos_graphics.metal by generate_windows_hlsl.py. */\n"
            "static const char kWindowsHlslSource[] =\n" +
            "\n".join(json.dumps(line + "\n") for line in hlsl.splitlines()) + ";\n"
            "static const char *const kWindowsHlslPasses[] = {" +
            ", ".join(json.dumps(name) for name in names) + "};\n"
            f"enum {{ kWindowsHlslPassCount = {len(names)} }};\n")
    Path(argv[2]).write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
