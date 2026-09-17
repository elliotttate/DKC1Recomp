"""Static contract for the opt-in first-level HD release preview."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HdReleaseMenuTests(unittest.TestCase):
    def test_menu_names_first_level_scope_and_persists_toggle(self):
        picker = (ROOT / "runner/macos_file_picker.m").read_text()
        pause = (ROOT / "runner/macos_pause_menu.m").read_text()
        host = (ROOT / "runner/sdl_host.c").read_text()

        self.assertIn(
            '@"Upscaled HD Textures (Jungle Hijinxs only)"', picker
        )
        self.assertIn("kDkc1MacMenuToggleHdTextures", picker)
        self.assertIn("kDkc1MacMenuToggleHdTextures", pause)
        self.assertIn("Unsupported scenes keep the original game textures", pause)
        self.assertIn("Dkc1MacSetHdTexturesEnabled(Dkc1HdEnabled())", host)
        self.assertIn('setBool:enabled != 0 forKey:@"DKC1HdTexturesEnabled"', picker)

    def test_pack_is_bundled_without_preview_save_state(self):
        picker = (ROOT / "runner/macos_file_picker.m").read_text()
        packaging = (ROOT / "build_macos.sh").read_text()

        self.assertIn('stringByAppendingPathComponent:@"preload.txt"', picker)
        self.assertIn('setenv("DKC1_HD_SCENE_PRELOAD","1",0)', picker)
        self.assertIn('setenv("DKC1_HD_CONNECTED_WORLD","1",0)', picker)
        self.assertIn('DKC1_HD_RELEASE_PACK', packaging)
        self.assertIn('ditto "$hd_pack" "$private_scene/Materials"', packaging)
        self.assertNotIn('entry.state', packaging)
        self.assertNotIn('DKC1_SAVESTATE_INPUT', picker)


if __name__ == "__main__":
    unittest.main()
