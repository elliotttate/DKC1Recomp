"""Mechanical syntax translation of the project's Metal shader arithmetic.

The Mac source remains authoritative and untouched. Generated headers contain
only project-owned shader text, no game data. Each pass is compiled separately.
"""
import json
import re
import sys
from pathlib import Path

def translate(source):
    source=source[source.index('fragment float4 dkc1_flat'):]
    signature=r'fragment float4 (dkc1_\w+)\([^\n]+\) \{'
    names=re.findall(signature,source)
    if names != ['dkc1_flat','dkc1_reconstruct','dkc1_lines','dkc1_beam','dkc1_down','dkc1_blur','dkc1_compose']:
        raise ValueError('Metal pass contract changed')
    source=re.sub(signature,r'vec4 \1() {',source)
    source=re.sub(r'texture2d<float>', 'sampler2D',source)
    source=source.replace('in.uv','v_uv').replace('in.position','gl_FragCoord')
    source=re.sub(r'\bfloat([234])\b',r'vec\1',source)
    source=re.sub(r'(\w+)\.sample\((pointSampler|linearSampler),',r'texture(\1,',source)
    source=source.replace('fmod(', 'mod(')
    source=re.sub(r'\bflat\b','flat_color',source)  # GLSL interpolation qualifier
    if '[[' in source or 'source.sample' in source:
        raise ValueError('Untranslated Metal syntax')
    prefix='#version 330 core\nin vec2 v_uv;\nout vec4 result;\nuniform sampler2D source, lines, image, glow, halo;\nuniform float u[27];\n'
    return [prefix+source+'\nvoid main(){ result='+name+'(); }\n' for name in names]

if __name__=='__main__':
    shaders=translate(Path(sys.argv[1]).read_text(encoding='utf-8'))
    Path(sys.argv[2]).write_text('/* Generated from macos_graphics.metal (shared GL 3.3 shader set for Windows+Linux). */\nstatic const char *const kGlShaders[]={\n'+',\n'.join(json.dumps(s) for s in shaders)+'\n};\n',encoding='utf-8')
