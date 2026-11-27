import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
# The SDK gate isolates manager detection from the installation side effects.
HARNESS = r'''
abort() { printf '%s\n' "$*" >&2; exit 90; }
ui_print() { printf '%s\n' "$*"; }
getprop() { printf 'SDK_CHECK_REACHED\n' >&2; printf '0\n'; }
. "$1"
'''


class InstallerManagerTests(unittest.TestCase):
    def install(self, **variables):
        environment = os.environ.copy()
        for name in ["MAGISK_VER_CODE", "BOOTMODE", "KSU", "APATCH"]:
            environment.pop(name, None)
        environment.update(BOOTMODE="true", **variables)
        return subprocess.run(["sh", "-c", HARNESS, "installer-test", str(ROOT / "module/customize.sh")],
                              env=environment, text=True, capture_output=True, timeout=5)

    def assert_passes_manager(self, result):
        self.assertEqual(result.returncode, 90)
        self.assertIn("SDK_CHECK_REACHED", result.stderr)
        self.assertIn("Supported range:", result.stderr)
        self.assertNotIn("Magisk 26 or newer is required", result.stderr)

    def test_magisk_21_is_rejected(self):
        result = self.install(MAGISK_VER_CODE="21000")
        self.assertEqual(result.returncode, 90)
        self.assertIn("Magisk 26 or newer is required", result.stderr)
        self.assertNotIn("SDK_CHECK_REACHED", result.stderr)

    def test_magisk_25_is_rejected(self):
        result = self.install(MAGISK_VER_CODE="25200")
        self.assertEqual(result.returncode, 90)
        self.assertIn("Magisk 26 or newer is required", result.stderr)
        self.assertNotIn("SDK_CHECK_REACHED", result.stderr)

    def test_magisk_26(self):
        result = self.install(MAGISK_VER_CODE="26000")
        self.assert_passes_manager(result)

    def test_newer_magisk(self):
        self.assert_passes_manager(self.install(MAGISK_VER_CODE="30000"))

    def test_old_magisk_is_rejected(self):
        result = self.install(MAGISK_VER_CODE="20999")
        self.assertEqual(result.returncode, 90)
        self.assertIn("Magisk 26 or newer is required", result.stderr)
        self.assertNotIn("SDK_CHECK_REACHED", result.stderr)

    def test_malformed_magisk_version_is_rejected(self):
        result = self.install(MAGISK_VER_CODE="25.2")
        self.assertEqual(result.returncode, 90)
        self.assertNotIn("SDK_CHECK_REACHED", result.stderr)

    def test_unknown_manager_is_rejected(self):
        result = self.install()
        self.assertEqual(result.returncode, 90)
        self.assertNotIn("SDK_CHECK_REACHED", result.stderr)

    def test_kernelsu_is_supported(self):
        self.assert_passes_manager(self.install(KSU="true"))

    def test_other_managers_do_not_show_magisk_warning(self):
        for manager in ("KSU", "APATCH"):
            with self.subTest(manager=manager):
                result = self.install(MAGISK_VER_CODE="20000", **{manager: "true"})
                self.assert_passes_manager(result)
                self.assertNotIn("SELinux or Magic Mount", result.stdout)

    def test_recovery_is_rejected(self):
        environment = os.environ.copy()
        environment.update(BOOTMODE="false", MAGISK_VER_CODE="26000")
        result = subprocess.run(["sh", "-c", HARNESS, "installer-test", str(ROOT / "module/customize.sh")],
                                env=environment, text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 90)
        self.assertIn("Install from the root manager app", result.stderr)
        self.assertNotIn("SDK_CHECK_REACHED", result.stderr)

    def test_magisk_without_preinit_is_rejected_before_installation(self):
        harness = r'''
abort() { printf '%s\n' "$*" >&2; exit 90; }
getprop() { printf '33\n'; }
magisk() { printf '%s\n' "$MAGISK_PATH"; }
. "$1"
'''
        with tempfile.TemporaryDirectory() as temporary:
            environment = os.environ.copy()
            environment.update(BOOTMODE="true", MAGISK_VER_CODE="26000",
                               MAGISKTMP=temporary, MAGISK_PATH=temporary,
                               MODPATH=str(Path(temporary) / "module"))
            result = subprocess.run(["sh", "-c", harness, "installer-test", str(ROOT / "module/customize.sh")],
                                    env=environment, text=True, capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 90)
            self.assertIn("Magisk preinit directory is missing", result.stderr)
            self.assertFalse((Path(temporary) / "module/stock").exists())

    def test_magisk_resolves_preinit_when_installer_path_is_missing_or_stale(self):
        harness = r'''
abort() { printf '%s\n' "$*" >&2; exit 90; }
getprop() { printf '33\n'; }
magisk() { printf '%s\n' "$MAGISK_PATH"; }
. "$1"
'''
        with tempfile.TemporaryDirectory() as temporary:
            tree = Path(temporary)
            (tree / ".magisk/preinit").mkdir(parents=True)
            module = tree / "module"
            module.mkdir()
            (module / "extract.sh").write_text("mb_extract_surfaceflinger() { return 1; }\n")
            for installer_path in (None, str(tree / "stale")):
                with self.subTest(installer_path=installer_path):
                    environment = os.environ.copy()
                    environment.pop("MAGISKTMP", None)
                    if installer_path is not None:
                        environment["MAGISKTMP"] = installer_path
                    environment.update(BOOTMODE="true", MAGISK_VER_CODE="26000",
                                       MAGISK_PATH=str(tree), MODPATH=str(module))
                    result = subprocess.run(["sh", "-c", harness, "installer-test",
                                             str(ROOT / "module/customize.sh")],
                                            env=environment, text=True, capture_output=True, timeout=5)
                    self.assertEqual(result.returncode, 90)
                    self.assertIn("Cannot capture the running system SurfaceFlinger", result.stderr)
                    self.assertNotIn("Magisk preinit directory is missing", result.stderr)


class MountedSystemTests(unittest.TestCase):
    def run_service(self, alias, launcher):
        # The storage-check sentinel exits before settings writes or controller startup.
        harness = r'''
getprop() { printf '1\n'; }
cmp() { command [ "$LAUNCHER_MATCH" = true ]; }
[() {
    if command [ "$1" = "!" ] && command [ "$2" = -x ] &&
       command [ "$3" = /data/misc/skia-motion-blur/runtime/surfaceflinger ]; then
        command [ "$ALIAS_MOUNTED" != true ]; return
    fi
    if command [ "$1" = "!" ] && command [ "$2" = -e ] && command [ "$3" = "${0%/*}/disable" ]; then
        printf 'SUPERVISION_REACHED\n'; exit 77
    fi
    command [ "$@"
}
. "$1"
'''
        with tempfile.TemporaryDirectory() as temporary:
            environment = os.environ.copy()
            environment.update(ALIAS_MOUNTED=str(alias).lower(), LAUNCHER_MATCH=str(launcher).lower())
            result = subprocess.run(["bash", "-c", harness, str(Path(temporary) / "runner"),
                                     str(ROOT / "module/service.sh")],
                                    env=environment, text=True, capture_output=True, timeout=5)
            error = Path(temporary) / "boot-error.txt"
            return result, error.read_text() if error.exists() else ""

    def test_missing_system_mount_stops_before_controller(self):
        result, error = self.run_service(False, False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("system files are not mounted", error)
        self.assertNotIn("SUPERVISION_REACHED", result.stdout)

    def test_another_launcher_mount_stops_before_controller(self):
        result, error = self.run_service(True, False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("system files are not mounted", error)

    def test_mounted_files_reach_controller_supervision(self):
        result, error = self.run_service(True, True)
        self.assertEqual(result.returncode, 77, result.stderr)
        self.assertIn("SUPERVISION_REACHED", result.stdout)
        self.assertEqual(error, "")


class InstalledLayoutTests(unittest.TestCase):
    def install_layout(self):
        with tempfile.TemporaryDirectory() as temporary:
            tree = Path(temporary)
            module = tree / "module"
            binaries = module / "bin/arm64-v8a"
            binaries.mkdir(parents=True)
            stock = tree / "surfaceflinger"
            original = bytearray(64)
            original[:6] = b"\x7fELF\x02\x01"
            original[18:20] = b"\xb7\x00"
            original = bytes(original)
            stock.write_bytes(original)
            # Model /proc executable links inside the temporary tree.
            process = tree / "proc/123"
            process.mkdir(parents=True)
            (process / "status").write_text("Uid:\t1000\t1000\t1000\t1000\n")
            (process / "cmdline").write_bytes(b"/system/bin/surfaceflinger\0")
            (process / "exe").symlink_to(stock)
            extraction = (ROOT / "module/extract.sh").read_text()
            extraction = extraction.replace("MB_STOCK_PROC=/proc/", f"MB_STOCK_PROC={tree}/proc/")
            (module / "extract.sh").write_text(extraction)
            for name in ("launcher", "controller", "libskiamotionblur.so"):
                binary = binaries / name
                binary.write_text(f"#!{shutil.which('sh')}\nexit 0\n")
                binary.chmod(0o755)
            for name in ("service.sh", "post-fs-data.sh", "late-load.sh", "uninstall.sh"):
                (module / name).write_text("")
            source = (ROOT / "module/customize.sh").read_text()
            script = tree / "customize.sh"
            script.write_text(source)
            harness = r"""
abort() { printf '%s\n' "$*" >&2; exit 90; }
ui_print() { :; }
getprop() { printf '33\n'; }
magisk() { printf '%s\n' "$MAGISK_PATH"; }
pidof() { printf '123\n'; }
set_perm_recursive() { :; }
set_perm() { chmod "$4" "$1"; }
. "$1"
"""
            environment = os.environ.copy()
            (tree / ".magisk/preinit").mkdir(parents=True)
            environment.update(MAGISK_VER_CODE="26000", BOOTMODE="true", MODPATH=str(module),
                               MAGISKTMP=str(tree), MAGISK_PATH=str(tree))
            result = subprocess.run(["sh", "-c", harness, "installer-test", str(script)],
                                    env=environment, text=True, capture_output=True, timeout=5)
            mounted = {str(path.relative_to(module / "system"))
                       for path in (module / "system").rglob("*") if path.is_file()}
            saved = module / "stock/surfaceflinger"
            return result, mounted, saved.read_bytes() if saved.exists() else None, original, (
                module / "payload/libskiamotionblur.so").is_file(), (module / "payload.sha256").is_file()

    def test_only_existing_compositor_entry_requires_mount(self):
        result, mounted, saved, original, payload, digest = self.install_layout()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(mounted, {"bin/surfaceflinger"})
        self.assertEqual(saved, original)
        self.assertTrue(payload and digest)

if __name__ == "__main__":
    unittest.main()
