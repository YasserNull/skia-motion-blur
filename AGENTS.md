# Repository Guidelines

## Project Structure

- `src/runtime/` separates EGL hooks, output filtering, resource tracking, GL state, renderer resources, composition control, and the launcher/controller.
- `module/` contains scripts and defaults for Magisk, KernelSU and APatch. System mounting must be enabled; preparation happens before mounting, and KernelSU late-load is rejected.
- `src/` contains the configuration parser, `config_cache.c` for snapshot ownership/reloading, and shared helpers in `utils.c`. Headers are in `src/include/`.
- `tools/` contains runtime build and verification scripts. `tests/` contains C and Python checks; `build/` is generated output.

## Build, Test, and Development

- `python tools/build_runtime.py --ndk /path/to/android-ndk` builds runtime binaries for Android.
- `python tools/verify_runtime.py` checks the built runtime artifacts.
- `cmake -S . -B build/host && cmake --build build/host && ctest --test-dir build/host --output-on-failure` builds and runs available host C tests.
- `python -m unittest discover -s tests` runs Python tooling and installer tests.

## Style and Naming

Use four spaces in C, Python, and shell. Native code uses C11 with `snake_case` functions and fields, `Mb`-prefixed structure types, and `MB_`-prefixed constants. Initialize structures and release owned allocations explicitly. Keep module scripts POSIX `sh` compatible unless Bash is required. Preserve generated artifacts only when the change intentionally updates them.

Keep code plain and direct, with lowercase `snake_case` filenames. Put reusable helpers in `utils.c`; keep subsystem state and resources in their own files. Use short English comments only where they explain a non-obvious constraint, decision, or behavior. Avoid decorative headers, obvious narration, change summaries, and comments announcing additions, fixes, or modifications. Keep change history in commit messages or documentation.

## Agent Output

Keep source code and comments in English. When asked to provide code, return raw code only, without explanatory prose or Markdown fences.

## Testing Guidance

Add focused regression coverage alongside the relevant suite: Python tests in `tests/test_*.py` or C checks in `tests/*_test.c`. Run the smallest relevant checks, then the broader host and runtime verification when changing shared behavior. `tests/gles_test.c` and `tests/hooks_test.c` require Android EGL/GLES headers and libraries and are not built by the host CMake configuration.

## Commits and Pull Requests

No commit-message convention is established in the available history. Use short imperative subjects that state the change (for example, `Fix installer mount validation`). PRs should explain behavior and device/API impact, list validation commands and results, link related issues, and include logs or screenshots for user-visible or device-specific changes. Avoid claiming device compatibility without evidence recorded in `VALIDATION.md`.

## Configuration and Safety

Do not execute `module/skia-motion-blur.prop` as shell code; it is a key-value configuration file. Keep system modification behavior within the root manager's module mount mechanism, and preserve the fail-closed behavior documented in `README.md`.

Use only the `skia-motion-blur` module identity and configuration paths. Do not add migration logic, compatibility aliases, or installation checks for previous module versions.

Capture the device compositor from the running SurfaceFlinger process during installation using `module/extract.sh`. Never bundle a device's SurfaceFlinger executable in the release ZIP. Record the captured source and checksum and retain boot-time verification.
