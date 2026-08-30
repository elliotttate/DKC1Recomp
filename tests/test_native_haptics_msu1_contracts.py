from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NativeHapticsMsu1ContractTests(unittest.TestCase):
    def test_stomp_probe_is_narrow_and_runs_around_one_cartridge_frame(self):
        haptics = (ROOT / "runner" / "dkc1_haptics.c").read_text(
            encoding="utf-8")
        host = (ROOT / "runner" / "sdl_host.c").read_text(encoding="utf-8")

        self.assertIn("kDkStompVelocity = 0x0720", haptics)
        self.assertIn("kDiddyStompVelocity = 0x0880", haptics)
        self.assertIn("probe->vertical_velocity >= 0", haptics)
        self.assertIn("Read16(wram, kActorState + slot) != 1", haptics)

        capture = host.index("Dkc1StompProbeCapture(&s_stomp_probe, g_ram)")
        run = host.index("RtlRunFrame(input)", capture)
        accepted = host.index("Dkc1StompProbeAccepted(&s_stomp_probe, g_ram)",
                              run)
        pulse = host.index("HapticWorkerRequest(kHapticRequestPulse)",
                           host.index("static void PulseStompHaptic"))
        worker = host.index("SDL_GameControllerRumble(controller, 0x2800",
                            host.index("static int SDLCALL HapticWorkerMain"))
        self.assertLess(capture, run)
        self.assertLess(run, accepted)
        self.assertGreater(pulse, 0)
        self.assertGreater(worker, 0)
        self.assertIn('SDL_CreateThread(HapticWorkerMain, "DKC1 haptics"',
                      host)

    def test_msu_overlay_is_checksum_gated_and_precedes_snes_init(self):
        msu = (ROOT / "runner" / "dkc1_msu1.c").read_text(encoding="utf-8")
        host = (ROOT / "runner" / "sdl_host.c").read_text(encoding="utf-8")

        self.assertIn("kSpcMuteRomOffset = 0x0AA9E5", msu)
        self.assertIn("{0x01, 0xD4}", msu)
        self.assertIn("{0x00, 0x6F}", msu)
        verified = host.index("Dkc1ReadVerifiedRom(")
        mute = host.index("Dkc1Msu1ApplySpcMusicMute(", verified)
        snes_init = host.index("SnesInit(rom", mute)
        self.assertLess(verified, mute)
        self.assertLess(mute, snes_init)

    def test_msu_follows_stock_request_and_start_state(self):
        msu = (ROOT / "runner" / "dkc1_msu1.c").read_text(encoding="utf-8")
        host = (ROOT / "runner" / "sdl_host.c").read_text(encoding="utf-8")
        self.assertIn("ReadWram16(0x0521)", host)
        self.assertIn("ReadWram16(0x051d)", host)
        self.assertNotIn("g_ram[0x4c]", host)
        self.assertIn("const bool started = start_state != 0", msu)
        self.assertIn("request_changed || start_changed", msu)
        self.assertIn("kLoopTheme[27]", msu)

    def test_external_pcm_is_mixed_after_stock_spc_audio(self):
        host = (ROOT / "runner" / "sdl_host.c").read_text(encoding="utf-8")
        render = host.index("RtlRenderAudio(s_audio_scratch")
        mix = host.index("Dkc1Msu1Mix(s_msu1, s_audio_scratch", render)
        queue = host.index("SDL_QueueAudio(s_audio_device", mix)
        self.assertLess(render, mix)
        self.assertLess(mix, queue)

    def test_msu_pcm_is_memory_mapped_before_frame_critical_mixing(self):
        msu = (ROOT / "runner" / "dkc1_msu1.c").read_text(encoding="utf-8")
        self.assertIn("mmap(", msu)
        self.assertIn("MADV_SEQUENTIAL", msu)
        self.assertIn("player->track->mapping + offset", msu)
        self.assertIn("for (unsigned track = 1; track <= kMsuTrackCount",
                      msu)
        self.assertNotIn("fread(", msu)

    def test_mac_menu_exposes_music_pack_controls(self):
        picker = (ROOT / "runner" / "macos_file_picker.m").read_text(
            encoding="utf-8")
        self.assertIn('@"Choose MSU-1 Music Pack…"', picker)
        self.assertIn('@"Disable Replacement Music"', picker)
        self.assertIn('@"/usr/bin/ditto"', picker)
        self.assertIn('@"DKC1Msu1Directory"', picker)


if __name__ == "__main__":
    unittest.main()
