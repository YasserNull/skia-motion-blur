import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SettingsTests(unittest.TestCase):
    def service(self, current=None):
        with tempfile.TemporaryDirectory() as temporary:
            tree = Path(temporary)
            module, data = tree / "module", tree / "data"
            module.mkdir()
            defaults = (ROOT / "module/skia-motion-blur.prop").read_bytes()
            settings = module / "skia-motion-blur.prop"
            settings.write_bytes(defaults if current is None else current)
            (data / "runtime").mkdir(parents=True)
            prepared = data / "runtime/surfaceflinger"
            prepared.touch()
            prepared.chmod(0o755)
            controller = module / "controller"
            controller.write_text('#!/bin/sh\ncat "$1/skia-motion-blur.prop" > "$1/observed"\ntouch "$1/disable"\n')
            controller.chmod(0o755)
            source = (ROOT / "module/service.sh").read_text().replace("/data/misc/skia-motion-blur", str(data))
            script = module / "service.sh"
            script.write_text(source)
            harness = 'getprop() { echo 1; }; cmp() { return 0; }; sleep() { :; }; . "$1"'
            result = subprocess.run(["sh", "-c", harness, str(script), str(script)],
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((module / "observed").read_bytes(), settings.read_bytes())
            return settings.read_bytes(), defaults

    def test_preserves_existing_settings(self):
        content, _ = self.service(current=b"enabled=0\nstrength=1\n")
        self.assertEqual(content, b"enabled=0\nstrength=1\n")

    def test_controller_receives_packaged_defaults(self):
        content, defaults = self.service()
        self.assertEqual(content, defaults)

    def test_uninstall_preserves_prepared_executables(self):
        with tempfile.TemporaryDirectory() as temporary:
            data = Path(temporary)
            (data / "runtime").mkdir()
            # Prepared binaries must outlive uninstall while the launcher may be mounted.
            stock, payload = data / "runtime/surfaceflinger", data / "runtime/libskiamotionblur.so"
            stock.write_bytes(b"original")
            payload.write_bytes(b"payload")
            for suffix in ("active", "owned", "prop", "launches", "status", "gles", "touch"):
                (data / f"skia-motion-blur.{suffix}").touch()
            source = (ROOT / "module/uninstall.sh").read_text().replace("/data/misc/skia-motion-blur", str(data))
            self.assertNotIn("/data/misc/", source)
            script = data / "uninstall.sh"
            script.write_text(source)
            environment = os.environ.copy()
            environment['TEST_SERVICE_LOG'] = str(data / "service.log")
            harness = 'service() { printf "%s\\n" "$*" >> "$TEST_SERVICE_LOG"; }; . "$1"'
            result = subprocess.run(["sh", "-c", harness, str(script), str(script)],
                                    env=environment, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(list(data.glob("skia-motion-blur.*")))
            self.assertEqual(stock.read_bytes(), b"original")
            self.assertEqual(payload.read_bytes(), b"payload")
            self.assertIn("SurfaceFlinger 1008 i32 0", (data / "service.log").read_text())


if __name__ == "__main__":
    unittest.main()
