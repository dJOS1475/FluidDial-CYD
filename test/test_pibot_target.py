"""Host regression checks; no pendant, flashing or downloaded C++ libraries needed."""

import json
import contextlib
import io
import os
from pathlib import Path
import shlex
import runpy
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TargetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        result = subprocess.run(
            ["pio", "project", "config", "--json-output"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        cls.config = {
            name: dict(options) for name, options in json.loads(result.stdout)
        }

    def compile(self, source, flags=()):
        return subprocess.run(
            shlex.split(os.environ.get("CXX", "c++"))
            + ["-std=c++11", "-x", "c++", "-fsyntax-only", "-I", str(ROOT / "src")]
            + list(flags)
            + ["-"],
            input=source,
            capture_output=True,
            text=True,
        )

    def test_pibot_build_flags_are_isolated(self):
        options = self.config["env:pibot_v42"]
        flags = options["build_flags"]
        for flag in (
            "-DPIBOT_PENDANT",
            "-DCAPACITIVE_CYD",
            "-DUSE_NEW_UI",
            "-DUSE_ESPNOW",
            "-DCYD_BUTTONS",
            "-DFNC_BAUD=1000000",
        ):
            self.assertIn(flag, flags)
        for flag in ("-DRESISTIVE_CYD", "-DCYD_BATTERY_ADC", "-DDEBUG_TO_USB"):
            self.assertNotIn(flag, flags)
        self.assertEqual(options["custom_artifact_subdir"], "pibot_v42")
        self.assertEqual(options["board_build.partitions"], "partitions_cyd_noota.csv")

    def test_grblparser_uses_full_commit_pin(self):
        deps = self.config["env:pibot_v42"]["lib_deps"]
        parser = next(dep for dep in deps if "MitchBradley/GrblParser#" in dep)
        # PlatformIO's shallow git fetch cannot resolve an abbreviated commit ID.
        self.assertRegex(parser.split("#", 1)[1], r"^[0-9a-f]{40}$")

    def test_standard_build_keeps_detection_and_battery(self):
        flags = self.config["env:cyd_new_ui_combined"]["build_flags"]
        for flag in (
            "-DRESISTIVE_CYD",
            "-DCAPACITIVE_CYD",
            "-DCYD_BATTERY_ADC",
            "-DUSE_NEW_UI",
            "-DUSE_ESPNOW",
        ):
            self.assertIn(flag, flags)
        for name, options in self.config.items():
            if name != "env:pibot_v42":
                self.assertNotIn("-DPIBOT_PENDANT", options.get("build_flags", []))
                self.assertNotIn("custom_artifact_subdir", options)
        self.assertEqual(
            self.config["platformio"]["default_envs"], ["m5dial", "cyddial"]
        )

    def test_pibot_pin_and_uart_mapping(self):
        result = self.compile(
            """
            #include "cnc_pendant_config.h"
            #include "Hardware2432.hpp"
            static_assert(TFT_BL == 21, "backlight");
            static_assert(ENCODER_CLK == 22 && ENCODER_DT == 27, "encoder");
            static_assert(ENCODER_SW == -1, "no encoder push switch");
            static_assert(BTN_RED_PIN == 4, "cancel");
            static_assert(BTN_YELLOW_PIN == 16, "pause");
            static_assert(BTN_GREEN_PIN == 17, "start");
            static_assert(pibot::touch_sda == 32 && pibot::touch_scl == 25, "touch");
            static_assert(PND_RX_FNC_TX_PIN == 1 && PND_TX_FNC_RX_PIN == 3, "UART");
            static_assert(FNC_UART_NUM == 0, "UART0");
            static_assert(pibot::selector_2 == 39, "GPIO39 is not battery ADC");
        """,
            ["-DPIBOT_PENDANT", "-DCAPACITIVE_CYD"],
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_standard_ui_pin_defaults_unchanged(self):
        result = self.compile("""
            #include "cnc_pendant_config.h"
            static_assert(TFT_BL == 27, "standard capacitive backlight");
            static_assert(ENCODER_CLK == 22 && ENCODER_DT == 21, "standard encoder");
            static_assert(BTN_YELLOW_PIN == 17 && BTN_GREEN_PIN == 16, "buttons");
        """)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_unsafe_pibot_flag_combinations_are_rejected(self):
        for flag, diagnostic in (
            ("-DRESISTIVE_CYD", "touch auto-detection"),
            ("-DCYD_BATTERY_ADC", "selector input"),
            ("-DDEBUG_TO_USB", "UART0 shares USB"),
            ("-DLED_DEBUG", "buttons, not debug LED"),
        ):
            with self.subTest(flag=flag):
                result = self.compile(
                    '#include "PiBotPins.h"',
                    ["-DPIBOT_PENDANT", "-DCAPACITIVE_CYD", flag],
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)


class PackagingTests(unittest.TestCase):
    def test_pibot_and_standard_artifacts_cannot_overwrite_each_other(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp)
            build = project / "build"
            build.mkdir()
            framework = project / "framework"
            boot_app0 = framework / "tools" / "partitions" / "boot_app0.bin"
            boot_app0.parent.mkdir(parents=True)
            boot_app0.write_bytes(b"boot app")
            names = (
                "merged-flash.bin",
                "bootloader.bin",
                "partitions.bin",
                "firmware.bin",
                "littlefs.bin",
            )
            for name in names:
                (build / name).write_bytes(b"standard")

            class FakeEnv:
                subdir = ""

                def BoardConfig(self):
                    return {"upload.flash_size": "4MB"}

                def get(self, key, default=None):
                    return default

                def subst(self, value):
                    return {"$BUILD_DIR": str(build), "$PROJECT_DIR": str(project)}.get(
                        value, value
                    )

                def GetProjectOption(self, key, default=None):
                    return {
                        "custom_artifact_subdir": self.subdir,
                        "custom_filesystem_start": "0x290000",
                    }.get(key, default)

                def PioPlatform(self):
                    return self

                def get_package_dir(self, name):
                    return str(framework)

                def AddCustomTarget(self, **kwargs):
                    self.target = kwargs

            env = FakeEnv()
            script = runpy.run_path(
                str(ROOT / "build_merged.py"),
                init_globals={"env": env, "Import": lambda name: None},
            )
            with contextlib.redirect_stdout(io.StringIO()):
                script["copy_to_docs"](None, None, env)
                env.subdir = "pibot_v42"
                for name in names:
                    (build / name).write_bytes(b"pibot")
                script["copy_to_docs"](None, None, env)
            for name in names:
                self.assertEqual((project / "docs" / name).read_bytes(), b"standard")
                self.assertEqual(
                    (project / "docs" / "pibot_v42" / name).read_bytes(), b"pibot"
                )
            self.assertTrue(
                (project / "docs" / "pibot_v42" / "boot_app0.bin").is_file()
            )


if __name__ == "__main__":
    unittest.main()
