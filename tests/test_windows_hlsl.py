"""Contracts for the Metal -> HLSL translation behind the Direct3D presenter."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('windows_hlsl', ROOT / 'scripts/generate_windows_hlsl.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
METAL = (ROOT / 'runner/macos_graphics.metal').read_text(encoding='utf-8')


class WindowsHlslContracts(unittest.TestCase):
    def test_every_mac_pass_becomes_an_entry_point(self):
        hlsl, names = module.translate(METAL)
        self.assertEqual(names[:1], ['dkc1_flat'])
        self.assertEqual(names[-1], 'dkc1_compose')
        for required in ('dkc1_reconstruct', 'dkc1_lines', 'dkc1_beam', 'dkc1_down', 'dkc1_blur'):
            self.assertIn(required, names)
        for name in names:
            self.assertIn(f'float4 {name}(VSOut input) : SV_Target', hlsl)
        self.assertIn('VSOut dkc1_vertex(uint id : SV_VertexID)', hlsl)
        self.assertIn('cbuffer Params : register(b0) { float u[27]; };', hlsl)

    def test_metal_syntax_is_fully_rewritten(self):
        hlsl, _ = module.translate(METAL)
        for forbidden in ('[[', 'texture2d<', '.sample(', 'fract(', 'mix(', 'in.uv', 'in.position', 'constexpr'):
            self.assertNotIn(forbidden, hlsl)
        # Arithmetic is untouched; only syntax changes.
        self.assertIn('4.0 * df(F, G)', hlsl)
        self.assertIn('0.06711056 * p.x + 0.00583715 * p.y', hlsl)
        # HLSL keyword collisions are renamed.
        self.assertIn('float3 pass_ = lerp(', hlsl)

    def test_single_argument_constructors_become_casts(self):
        self.assertEqual(module.castify('float3(0.0)'), '((float3)(0.0))')
        self.assertEqual(module.castify('max(a, float2(1))'), 'max(a, ((float2)(1)))')
        self.assertEqual(module.castify('float2(u[0],u[1])'), 'float2(u[0],u[1])')
        self.assertEqual(module.castify('float4(source.Sample(s, uv).rgb, 1)'),
                         'float4(source.Sample(s, uv).rgb, 1)')
        self.assertEqual(module.castify('uint2(clamp(floor(x), float2(0), y))'),
                         '((uint2)(clamp(floor(x), ((float2)(0)), y)))')
        with self.assertRaises(ValueError):
            module.castify('float3(')

    def test_untranslated_syntax_fails_closed(self):
        with self.assertRaises(ValueError):
            module.translate(METAL.replace('fragment float4 dkc1_flat', 'fragment float4 nope'))


if __name__ == '__main__':
    unittest.main()
