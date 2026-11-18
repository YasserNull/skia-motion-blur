import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[1]
METADATA = dict(line.split("=", 1) for line in (ROOT / "module/module.prop").read_text().splitlines() if "=" in line)
ARCHIVE = ROOT / "release" / f"skia-motion-blur-{METADATA['version']}.zip"
ABIS = {"arm64-v8a": (2, 183), "armeabi-v7a": (1, 40), "x86": (1, 3), "x86_64": (2, 62)}


def elf(data, abi):
    kind, machine = ABIS[abi]
    assert data[:4] == b"\x7fELF" and data[4] == kind and data[5] == 1
    assert struct.unpack_from("<H", data, 18)[0] == machine
    assert struct.unpack_from("<H", data, 16)[0] == 3
    # Program-header offsets differ between ELF32 and ELF64.
    if kind == 2:
        offset = struct.unpack_from("<Q", data, 32)[0]
        size, count = struct.unpack_from("<HH", data, 54)
    else:
        offset = struct.unpack_from("<I", data, 28)[0]
        size, count = struct.unpack_from("<HH", data, 42)
    loads = 0
    for index in range(count):
        start = offset + size * index
        # PT_LOAD segments are the regions the kernel maps into memory.
        if struct.unpack_from("<I", data, start)[0] == 1:
            alignment = struct.unpack_from("<Q" if kind == 2 else "<I", data, start + (48 if kind == 2 else 28))[0]
            assert alignment >= 16384, (abi, alignment)
            loads += 1
    assert loads > 0


def main():
    with zipfile.ZipFile(ARCHIVE) as archive:
        assert archive.testzip() is None
        names = set(archive.namelist())
        for required in ["module.prop", "customize.sh", "post-fs-data.sh", "service.sh", "uninstall.sh",
                         "sepolicy.rule", "skia-motion-blur.prop", "README.md", "LICENSE"]:
            assert required in names
        manifest = json.loads(archive.read("binaries.sha256.json"))
        expected = {f"bin/{abi}/{binary}" for abi in ABIS for binary in ["launcher", "controller", "libskiamotionblur.so"]}
        assert set(manifest) == expected
        for name, digest in manifest.items():
            data = archive.read(name)
            assert hashlib.sha256(data).hexdigest() == digest
            elf(data, name.split("/")[1])
            # Embedded paths catch binaries built for a different runtime directory.
            if name.endswith("/launcher"):
                for required_path in (
                    b"/data/misc/skia-motion-blur/runtime/libskiamotionblur.so\0",
                    b"/data/misc/skia-motion-blur/runtime/surfaceflinger\0",
                    b"/data/misc/skia-motion-blur/skia-motion-blur.launches\0",
                ):
                    assert required_path in data, (name, required_path)
            if name.endswith("/controller"):
                assert b"u:object_r:skia_motion_blur_data_file:s0\0" in data, name
            if not name.endswith("/launcher"):
                assert b"/data/misc/skia-motion-blur/skia-motion-blur.prop\0" in data, name
        metadata = dict(line.split("=", 1) for line in archive.read("module.prop").decode().splitlines())
        assert metadata["id"] == "skia-motion-blur" and metadata["name"] == "Skia Motion Blur" and metadata["author"] == "YasserNull"
        assert archive.read("module.prop") == (ROOT / "module/module.prop").read_bytes()
        assert archive.read("skia-motion-blur.prop") == (ROOT / "module/skia-motion-blur.prop").read_bytes()
        assert not any(name.startswith("zygisk/") for name in names)
        # Script equality catches stale packaging even when binary hashes match.
        for path in (ROOT / "module").rglob("*"):
            if not path.is_file():
                continue
            if path.suffix == ".sh" or path.name == "update-binary":
                subprocess.run(["sh", "-n", str(path)], check=True)
            assert archive.read(path.relative_to(ROOT / "module").as_posix()) == path.read_bytes()
        assert archive.read("README.md") == (ROOT / "README.md").read_bytes()
    print("PASS: ZIP integrity, identity, 12 ELF binaries, ABI, 16KB alignment, SHA-256, shell syntax, packaged sources")


if __name__ == "__main__":
    main()
