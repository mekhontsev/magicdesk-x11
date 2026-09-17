# MagicDesk X11

Native X11 engine for [MagicDesk](https://github.com/mekhontsev/magicdesk),
maintained as a focused fork of [Termux:X11](https://github.com/termux/termux-x11).
This working tree contains C/C++ server and renderer code, dependencies, native
tests and CMake recipes. All Java, Binder, JNI and Android application integration
belongs to MagicDesk's own x11-runtime module, not this fork.

## Native Engine

- Independent X servers, display sockets and Xauthority authentication.
- Composite/Damage window discovery and multiple native output surfaces.
- Individual window families or a whole X screen through the same output path.
- Pointer, keyboard and Unicode input, geometry, titles and bounded icons.
- Live X11 DPI, with optional XSettings ownership for application sessions.
- Clipboard and copy drag-and-drop negotiation for text, HTML, PNG and files.
- AHardwareBuffer/EGL and optional Vulkan DMA-BUF copies, with CPU fallback.
- Explicit connection replacement and native shutdown, without Java callbacks.

The public C contract is [embedded.h](lorie/src/main/cpp/lorie/embedded.h).
The host owns authorization, process lifetime, clipboard grants, IME and Android
window placement. No firmware-specific display or graphics API is required.

## Build

Initialize submodules recursively. Use Android NDK 27.3.13750724, CMake 3.22+,
Ninja, Python 3, Bison, patch and a host C compiler. API 34 is the native floor;
successful compilation is not device coverage. Android 14 testing remains pending.

```sh
cmake -S lorie/src/main/cpp -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-34 -DLORIE_BUILD_SMOKE=ON
cmake --build build --target lorie-smoke --parallel 4
sh scripts/verify-native.sh
```

The LorieNative CMake target is linked into the host's library. The smoke host
links and exercises server readiness, connection ownership and normal shutdown
without a JVM. Windows uses MSYS2 Bison/patch and UCRT64 for host generators,
with the NDK for Android code. Termux can use its native Clang toolchain.
CI targets Android `arm64-v8a` on both Linux and Windows build hosts.

[Embedding](docs/embedding.md) describes native ownership, transport, rendering
and fixtures. [Maintenance](docs/maintenance.md) describes upstream merges.
Android builds and lifecycle tests live in
[MagicDesk x11-runtime](https://github.com/mekhontsev/magicdesk/tree/main/x11-runtime).

## Upstream And License

The standalone upstream APK and loader are intentionally removed from this
working branch. Original Git ancestry and native paths remain for upstream
merges; use upstream/master for the original complete application.
[GNU GPL version 3](LICENSE), with original copyright and dependency notices
preserved. Corresponding source includes the host adapter, this pinned fork,
recursively pinned dependencies and build recipes.
