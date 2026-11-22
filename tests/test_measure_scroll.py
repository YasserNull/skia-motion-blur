import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ScrollMeasurementTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.data = self.root / "data"
        self.data.mkdir()
        self.settings = self.root / "skia-motion-blur.prop"
        self.mirror = self.data / self.settings.name
        self.set_config("enabled=1\ntest=balanced\n")
        (self.data / "skia-motion-blur.status").write_text("foreground=com.android.settings\n")
        self.script = self.root / "measure-scroll.sh"
        source = (ROOT / "module" / self.script.name).read_text()
        self.script.write_text(source.replace("/data/misc/skia-motion-blur", str(self.data)))
        self.stub("wm", 'echo "Physical size: 1080x2400"')
        self.stub("sleep", ":")
        self.stub("input", '''
i=$(cat "$TEST_DATA/count" 2>/dev/null || echo 0)
i=$((i + 1))
echo "$i" > "$TEST_DATA/count"
[ ! -f "$TEST_DATA/stale" ] || exit 0
printf '%s\\ntest=balanced\\nguide=compute\\ncomposite=fetch-ext\\n' "$i" > "$TEST_DATA/skia-motion-blur.gles"
''')
        self.stub("dumpsys", '''
[ "$3" != reset ] || exit 0
printf 'Total frames rendered: 100\\nJanky frames: 5 (5.00%%)\\n50th percentile: 16ms\\n95th percentile: 19ms\\n'
''')

    def set_config(self, content):
        self.settings.write_text(content)
        self.mirror.write_text(content)

    def stub(self, name, body):
        script = self.root / name
        script.write_text("#!/bin/sh\n" + body + "\n")
        script.chmod(0o755)

    def measure(self):
        env = dict(os.environ, PATH=str(self.root) + ":" + os.environ["PATH"], TEST_DATA=str(self.data))
        return subprocess.run(["sh", self.script], env=env, capture_output=True, text=True, timeout=5)

    def test_reports_active_path_and_app_percentiles(self):
        result = self.measure()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("composite=fetch-ext", result.stdout)
        self.assertIn("95th percentile: 19ms", result.stdout)
        self.assertEqual((self.data / "count").read_text().strip(), "14")

    def test_waits_for_controller_and_requires_fresh_frames(self):
        self.settings.write_text("test=fetch\n")
        self.assertNotEqual(self.measure().returncode, 0)
        self.assertFalse((self.data / "count").exists())
        self.set_config("enabled=1\ntest=balanced\n")
        (self.data / "stale").touch()
        self.assertIn("No fresh GLES frame", self.measure().stderr)

    def test_off_baseline_requires_an_inactive_controller(self):
        self.set_config("enabled=0\n")
        result = self.measure()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("enabled=0", result.stdout)
        (self.data / "skia-motion-blur.active").touch()
        self.assertNotEqual(self.measure().returncode, 0)
