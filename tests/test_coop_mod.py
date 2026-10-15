from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CoopModTests(unittest.TestCase):
    def test_native_scripted_kong_reads_his_own_physical_controller(self):
        rom = ROOT / "dkc1.sfc"
        if not rom.exists():
            self.skipTest("supported ROM required for native input continuation")
        data = rom.read_bytes()[0x3fb2c7:0x3fb2e1]
        source = (ROOT / "tests/coop_native_pad_harness.c").read_text(encoding="utf-8")
        source = source.replace("/* ROM_B2C7 */", ",".join(str(b) for b in data))
        self.compile_harness(source, ["runner/dkc1_coop_movement.c",
            "tests/coop_test_support.c", "snesrecomp/runner/src/snes/interp816.c"])

    def test_ceiling_light_projection_matches_native_nmi_registers(self):
        rom_path = ROOT / "dkc1.sfc"
        if not rom_path.exists():
            self.skipTest("private supported ROM unavailable for native light oracle")
        rom = rom_path.read_bytes()
        source = (ROOT / "tests/coop_light_harness.c").read_text()
        for start, end in ((0xB1BA, 0xB206), (0xB206, 0xB375), (0xB38B, 0xB3D7),
                           (0xB3D7, 0xB490), (0xB90B, 0xB916)):
            source = source.replace(f"/* ROM_{start:04X} */",
                ",".join(str(b) for b in rom[start:end]))
        self.compile_harness(source, ["runner/dkc1_coop_movement.c",
            "tests/coop_test_support.c", "snesrecomp/runner/src/snes/interp816.c"])

    def compile_overlay_harness(self, filename):
        self.compile_harness((ROOT / "tests" / filename).read_text(encoding="utf-8"),
                             ["runner/dkc1_coop_movement.c",
                              "tests/coop_test_support.c"])

    def test_movement_and_input_routing(self):
        rom = ROOT / "dkc1.sfc"
        if rom.exists():
            # Free animal lifetime uses absolute horizontal distance < $200.
            self.assertEqual(rom.read_bytes()[0x3fcada:0x3fcafa], bytes.fromhex(
                "22 9c 9b 80 a6 82 b9 19 0b 38 fd 19 0b 10 04 "
                "49 ff ff 1a c9 00 02 30 06 22 76 f4 bd 38 60 18 60"))
        self.compile_harness((ROOT / "tests/coop_movement_harness.c").read_text(),
                             ["runner/dkc1_coop_movement.c", "runner/desktop_input.c",
                              "tests/coop_test_support.c"])

    def test_banana_pickup_preserves_neighbours_and_spawns_one_flight(self):
        self.compile_overlay_harness("coop_banana_harness.c")

    def test_rambi_link_and_palette_survive_state_transitions(self):
        self.compile_overlay_harness("coop_rambi_harness.c")

    def test_frame_clock_actor_transactions_and_state_continuity(self):
        source = (ROOT / "tests/coop_runtime_harness.c").read_text()
        rom_path = ROOT / "dkc1.sfc"
        if rom_path.exists():
            # Rope ascent adds authored drift, then rejects movement at row < $28.
            self.assertEqual(rom_path.read_bytes()[0x3FA095:0x3FA0B3], bytes.fromhex(
                "ac 01 1b a6 82 b9 c5 14 18 7d 8d 0f 30 0f a5 4a "
                "fd c1 0b 38 ed 95 08 38 e9 28 00 30 01 60"))
            self.assertEqual(rom_path.read_bytes()[0x36AA89:0x36AAB7],
                             rom_path.read_bytes()[0x36F553:0x36F581])
            data = rom_path.read_bytes()[0x36F553:0x36F581]
            source = source.replace("/* ROM_F553 */", ",".join(str(b) for b in data) + ",")
        self.compile_harness(source, ["runner/dkc1_coop_movement.c",
            "tests/coop_test_support.c", "snesrecomp/runner/src/snes/interp816.c"])

    def test_independent_death_preserves_native_second_death_and_barrel_revival(self):
        rom_path = ROOT / "dkc1.sfc"
        if not rom_path.exists():
            self.skipTest("private supported ROM unavailable for native continuations")
        rom = rom_path.read_bytes()
        source = (ROOT / "tests/coop_damage_harness.c").read_text()
        for start, end in ((0xA3F8, 0xA40E), (0xA189, 0xA1EF), (0xA1F1, 0xA210), (0xC9D3, 0xCA15),
                           (0xA6D1, 0xA6F8), (0xA85F, 0xA867),
                           (0xC927, 0xC9A2), (0xC906, 0xC927), (0x88B4, 0x88D8), (0x83A8, 0x83B0),
                           (0x9EC2, 0x9ED0), (0x840C, 0x841A), (0xA0D5, 0xA0E4), (0x93CF, 0x93E2)):
            bank = 0 if start == 0x83A8 else 0x380000 if start == 0x840C else 0x360000 if start == 0xA85F else 0x3F0000
            data = rom[bank + start:bank + end]
            source = source.replace(f"/* ROM_{start:04X} */", ",".join(str(b) for b in data))
        self.compile_harness(source, ["runner/dkc1_coop_movement.c",
            "tests/coop_test_support.c", "snesrecomp/runner/src/snes/interp816.c"])

    def test_frame_hooks_use_the_runtime_selected_input(self):
        game = (ROOT / "runner/dkc1_game.c").read_text()
        frame = game.split("static void Dkc1RunOneFrame(void)", 1)[1].split("static void Dkc1SaveExtra", 1)[0]
        self.assertEqual(frame.count("Dkc1CoopBeginFrame("), 1)
        self.assertEqual(frame.count("Dkc1CoopEndFrame("), 1)
        self.assertEqual(frame.count("Dkc1CoopWorldNoteFrame("), 1)
        self.assertLess(frame.index("Dkc1CoopWorldNoteFrame"), frame.index("Dkc1CoopWorldRunScanner"))
        self.assertNotIn("Dkc1CoopWorldNoteFrame", (ROOT / "runner/dkc1_coop_view.c").read_text())
        self.assertIn("g_snes->input1_currentState", frame)
        self.assertIn("g_snes->input2_currentState", frame)
        self.assertLess(frame.index("Dkc1CoopBeginFrame"), frame.index("interp_bridge_run_until_quiescent"))
        self.assertGreater(frame.index("Dkc1CoopEndFrame"), frame.rindex("interp_bridge_run_until_quiescent"))
        for host in ("sdl_host.c", "win32_host.c", "headless_main.c"):
            source = (ROOT / "runner" / host).read_text()
            self.assertNotIn("Dkc1CoopNoteHostInput", source)
        video = (ROOT / "runner/dkc1_video.c").read_text()
        end = video.split("void Dkc1VideoEndPlacedActorDispatch", 1)[1].split("\n}", 1)[0]
        self.assertIn("if (!s_prefetch_dispatch_active)", end)
        self.assertIn("Dkc1CoopAfterActor", end)

    def test_two_identical_controllers_are_both_opened(self):
        # Compile the actual enumeration function with two SDL device fixtures.
        # Their model GUIDs match; their live instance IDs are different.
        source = (ROOT / "runner/sdl_host.c").read_text()
        function = source.split("static void OpenFirstController(void) {", 1)[1].split("\n}", 1)[0]
        fixture = (ROOT / "tests/coop_controllers_harness.c").read_text()
        self.compile_harness(fixture.replace("/* ENUMERATE_CONTROLLERS */",
                             "static void OpenFirstController(void) {" + function + "\n}"))

    def compile_harness(self, source, extra_sources=()):
        compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("cl")
        devcmd = None
        if compiler is None and os.name == "nt":
            vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
            if vswhere.exists():
                found = subprocess.run(
                    [str(vswhere), "-latest", "-products", "*", "-requires",
                     "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                    capture_output=True, text=True, check=True,
                ).stdout.strip()
                if found:
                    devcmd = Path(found) / "Common7/Tools/VsDevCmd.bat"
                    compiler = "cl"
        if compiler is None:
            self.skipTest("a C compiler is required for the co-op contracts")
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "coop_contract.exe"
            includes = [ROOT / "runner", ROOT / "tests",
                        ROOT / "snesrecomp/runner/src", ROOT / "snesrecomp/runner/src/snes"]
            src = Path(tmp) / "contract.c"
            src.write_text(source, encoding="utf-8")
            sources = [src] + [ROOT / name for name in extra_sources]
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                args = [compiler, "/nologo", "/std:c11", "/W0", "/O2"]
                args += ["/I" + str(p) for p in includes]
                args += [str(p) for p in sources] + ["/Fe:" + str(exe), "/link", "advapi32.lib"]
            else:
                args = [compiler, "-std=c11", "-O2"]
                args += ["-I" + str(p) for p in includes]
                args += [str(p) for p in sources] + ["-o", str(exe)]
                if os.name == "nt":
                    args += ["-ladvapi32"]
            if devcmd:
                batch = Path(tmp) / "compile.bat"
                batch.write_text('@echo off\ncall "' + str(devcmd) + '" -arch=x64 -no_logo\n' +
                                 subprocess.list2cmdline(args) + '\n', encoding="utf-8")
                args = ["cmd", "/c", str(batch)]
            built = subprocess.run(args, cwd=tmp, capture_output=True, text=True)
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            env = os.environ.copy()
            ran = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
            self.assertEqual(0, ran.returncode, ran.stdout + ran.stderr)
