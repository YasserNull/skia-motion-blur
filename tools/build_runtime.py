import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "arm64-v8a": "aarch64-linux-android31",
    "armeabi-v7a": "armv7a-linux-androideabi31",
    "x86": "i686-linux-android31",
    "x86_64": "x86_64-linux-android31",
}


def shaders():
    common = (ROOT / "src/runtime/motion.glsl").read_text()
    vertex = """#version 300 es
precision highp float;
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
"""
    prefix = """#version 300 es
precision highp float;
precision highp int;
in vec2 uv;
out vec4 color;
uniform sampler2D previousFrame;
uniform sampler2D currentFrame;
uniform sampler2D flowFrame;
uniform sampler2D guideFrame;
uniform sampler2D currentHistoryFrame;
uniform vec2 mbExtent;
uniform vec2 mbFlowScale;
uniform float mbStrength;
uniform float mbDebug;
"""
    sources = {"vertexShader": vertex,
               "guideShader": prefix + common + "\nvoid main() { color = mbGuide(); }\n",
               "motionShader": prefix + common + "\nvoid main() { color = mbMotion(uv); }\n",
               "resolveShader": prefix + common + "\nvoid main() { color = mbResolve(uv); if (mbDebug < 0.5 && !mbBlurred) discard; }\n"}
    output = ROOT / "build/runtime/generated"
    output.mkdir(parents=True, exist_ok=True)
    # JSON escaping preserves GLSL newlines and quotes in the C string literals.
    (output / "shaders.h").write_text("#pragma once\n" + "\n".join(
        f"static const char {name}[] = {json.dumps(source)};" for name, source in sources.items()) + "\n")
    for name, source in sources.items():
        (output / (name.replace("Shader", "_shader") + ".glsl")).write_text(source)
    return output


def build(ndk, abis):
    # Device compositor snapshots belong to installation, never to build inputs.
    for name in ("stock", "stock.sha256", "stock-source.txt", "system/bin/surfaceflinger"):
        if (ROOT / "module" / name).exists():
            raise RuntimeError("Device compositor files must be captured during installation")
    # Resolve aliases before enforcing a single host toolchain.
    candidates = sorted({path.resolve() for path in (ndk / "toolchains/llvm/prebuilt").glob("*/bin/clang")})
    if len(candidates) != 1:
        raise RuntimeError("Select an NDK with one host toolchain")
    compiler = candidates[0]
    generated = shaders()
    stage = ROOT / "build/runtime/package"
    # Fresh staging excludes binaries left by a different ABI selection.
    if stage.exists():
        shutil.rmtree(stage)
    shutil.copytree(ROOT / "module", stage)
    env = os.environ.copy()
    # Host library overrides can make the NDK load incompatible libraries.
    env.pop("LD_LIBRARY_PATH", None)
    for abi in abis:
        destination = stage / "bin" / abi
        destination.mkdir(parents=True)
        base = [str(compiler), f"--target={TARGETS[abi]}", "-std=c11", "-O2", "-fPIC",
                '-DSKIA_MOTION_BLUR_CONFIG_PATH="/data/misc/skia-motion-blur/skia-motion-blur.prop"', "-fvisibility=hidden", "-Wall", "-Wextra", "-Werror",
                "-Wl,-z,max-page-size=16384", "-Wl,-z,relro,-z,now", "-Wl,--build-id=sha1",
                "-I" + str(generated), "-I" + str(ROOT / "src/include")]
        shared = [str(ROOT / "src" / name) for name in
                  ("skia_motion_blur_config.c", "config_cache.c", "utils.c")]
        for name, inputs, flags in [
            ("launcher", ["launcher.c"], []),
            ("controller", ["controller.c", "control.c", "focus.c"], shared + ["-llog"]),
            ("libskiamotionblur.so", ["hooks.c", "filter.c", "tracking.c", "renderer.c",
                                    "renderer_resources.c", "gl_state.c", "repaint.c"],
             shared + ["-shared", "-Wl,--exclude-libs,ALL",
              "-Wl,--version-script=" + str(ROOT / "src/runtime/exports.map"),
              "-lEGL", "-lGLESv3", "-landroid", "-llog", "-ldl", "-lbinder_ndk"]),
        ]:
            command = base + [str(ROOT / "src/runtime" / name) for name in inputs] + flags + ["-o", str(destination / name)]
            subprocess.run(command, env=env, check=True)
            print(f"Built {abi}/{name}", flush=True)
    manifest = {str(path.relative_to(stage)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted((stage / "bin").rglob("*")) if path.is_file()}
    (stage / "binaries.sha256.json").write_text(json.dumps(manifest, indent=2) + "\n")
    for document in ["README.md", "VALIDATION.md"]:
        shutil.copy(ROOT / document, stage / document)
    shutil.copy(ROOT / "LICENSE", stage / "LICENSE")
    metadata = dict(line.split("=", 1) for line in (ROOT / "module/module.prop").read_text().splitlines() if "=" in line)
    archive = ROOT / "release" / f"skia-motion-blur-{metadata['version']}.zip"
    archive.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                output.write(path, path.relative_to(stage))
    print(archive)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--abi", choices=TARGETS, action="append")
    args = parser.parse_args()
    build(args.ndk, args.abi or list(TARGETS))
