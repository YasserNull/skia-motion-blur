import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import re


ROOT = Path(__file__).resolve().parents[1]
# Stub ownership, SELinux labeling and loading while keeping file checks real.
HARNESS = r'''
chown() { return 0; }
chcon() {
    case "$1" in
        "u:object_r:$TEST_DATA_TYPE:s0"|u:object_r:surfaceflinger_exec:s0|u:object_r:system_lib_file:s0) ;;
        *) return 1 ;;
    esac
    [ "$TEST_LABEL_FAIL" != 1 ]
}
sync() { return 0; }
env() {
    printf 'probe\n' >> "$TEST_PROBE_LOG"
    [ "$TEST_PROBE_FAIL" != 1 ]
}
cp() {
    if [ "$TEST_COPY_FAIL" = 1 ]; then return 1; fi
    command cp "$@"
}
. "$1"
'''


class BootPreparationTests(unittest.TestCase):
    def prepare(self, mode="success"):
        with tempfile.TemporaryDirectory() as temporary:
            tree = Path(temporary)
            module = tree / "module"
            data = tree / "data"
            system = tree / "surfaceflinger"
            (module / "stock").mkdir(parents=True)
            (module / "payload").mkdir()
            stock, payload = b"original-surfaceflinger", b"motion-blur-library"
            system.write_bytes(stock)
            (module / "stock/surfaceflinger").write_bytes(stock)
            (module / "payload/libskiamotionblur.so").write_bytes(payload)
            for name, content in [("stock", stock), ("payload", payload)]:
                (module / f"{name}.sha256").write_text(hashlib.sha256(content).hexdigest() + "\n")
            source = (ROOT / "module/post-fs-data.sh").read_text()
            source = source.replace("/data/misc/skia-motion-blur", str(data)).replace("/system/bin/surfaceflinger", str(system))
            script = module / "post-fs-data.sh"
            script.write_text(source)
            if mode == "firmware-change": system.write_bytes(b"updated-firmware")
            if mode == "missing-stock": (module / "stock/surfaceflinger").unlink()
            if mode == "corrupt-payload": (module / "payload/libskiamotionblur.so").write_bytes(b"corrupt")
            environment = os.environ.copy()
            policy = (ROOT / "module/sepolicy.rule").read_text()
            # Derive the accepted label from policy to catch script/policy drift.
            data_type = re.search(r"^type ([A-Za-z_][A-Za-z0-9_]*) ", policy).group(1)
            self.assertEqual(data_type, "skia_motion_blur_data_file")
            environment.update(TEST_LABEL_FAIL=str(int(mode == "label-failure")),
                               TEST_DATA_TYPE=data_type,
                               TEST_COPY_FAIL=str(int(mode == "copy-failure")),
                               TEST_PROBE_FAIL=str(int(mode == "probe-failure")),
                               TEST_PROBE_LOG=str(tree / "probe-log"))
            result = subprocess.run(["sh", "-c", HARNESS, str(script), str(script)],
                                    env=environment, capture_output=True, text=True, timeout=5)
            if mode == "success":
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((data / "runtime/surfaceflinger").read_bytes(), stock)
                self.assertEqual((data / "runtime/libskiamotionblur.so").read_bytes(), payload)
                self.assertTrue((data / "runtime/surfaceflinger").stat().st_mode & 0o100)
                self.assertEqual((data / "skia-motion-blur.launches").read_text(), "0\n")
                self.assertFalse((module / "skip_mount").exists())
                self.assertEqual((tree / "probe-log").read_text(), "probe\n")
            else:
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertTrue((module / "disable").exists())
                self.assertTrue((module / "skip_mount").exists())
                self.assertTrue((module / "boot-error.txt").read_text().strip())

    def test_prepares_runtime_before_mount(self): self.prepare()
    def test_changed_firmware_skips_mount(self): self.prepare("firmware-change")
    def test_missing_original_skips_mount(self): self.prepare("missing-stock")
    def test_corrupt_payload_skips_mount(self): self.prepare("corrupt-payload")
    def test_copy_failure_skips_mount(self): self.prepare("copy-failure")
    def test_label_failure_skips_mount(self): self.prepare("label-failure")
    def test_loader_failure_skips_mount(self): self.prepare("probe-failure")


if __name__ == "__main__":
    unittest.main()
