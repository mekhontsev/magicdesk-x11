# MagicDesk X11

Embeddable X11 runtime for [MagicDesk](https://github.com/mekhontsev/magicdesk),
maintained as a focused fork of [Termux:X11](https://github.com/termux/termux-x11).
The `embedded` Android library provides independently retained server
connections and multiple output surfaces. Each output selects an individual X11
window with its transient family, or an entire X screen. The standalone
Termux:X11 Android application is not required by an embedding host.

## Embedded Runtime

- Independent X servers, display sockets, authentication and input focus.
- Composite/Damage window discovery and content-only Android output surfaces.
- Mouse, keyboard and Unicode input, resize, window titles and bounded icons.
- Live X11 DPI, with optional XSettings ownership for application sessions.
- Clipboard and copy drag-and-drop negotiation for text, HTML, PNG and files,
  with streamed selections and explicit host-side Android URI grants.
- AHardwareBuffer/EGL rendering and optional Vulkan copies for compatible
  linear DMA-BUFs, retaining CPU fallback when unsupported.
- Binder ownership for startup admission, normal shutdown and owner-death cleanup.

The library owns X11 protocol and rendering, not Android task placement,
Desktop/HOME, launcher discovery or permission policy. MagicDesk supplies those
host responsibilities and the Termux execution environment. No firmware-specific
display or graphics API is required.

## Build And Integrate

The embedded library targets Android 14 / API 34 and newer. Device coverage
must be established separately from a successful build.

```sh
git clone --recurse-submodules https://github.com/mekhontsev/magicdesk-x11.git
cd magicdesk-x11
./gradlew -p examples/android :app:assembleDebug :app:lintDebug :embedded:lintDebug
```

Use JDK 17+, Android SDK 37, NDK 27.3.13750724, CMake, Ninja, Python 3, Bison,
patch and a host C compiler. Native arm64 Termux builds and Windows/MSYS2 builds
have additional setup in [Embedding](docs/embedding.md).

[Embedding](docs/embedding.md) defines the authenticated bootstrap, session/output
lifetimes, input, clipboard/drag protocols, rendering limits and executable
fixtures. [Maintenance](docs/maintenance.md) describes upstream merges and
verification. For end-user installation, application discovery and proot/chroot
launching, use [MagicDesk's X11 guide](https://github.com/mekhontsev/magicdesk/blob/main/docs/x11.md).

## Upstream Standalone Application

The original module paths and standalone app remain available to keep upstream
merges practical. Their separate APK, companion package and launcher are not
the embedded integration. Use the
[upstream instructions](https://github.com/termux/termux-x11#setup-instructions)
when working with the standalone application.

## License

[GNU GPL version 3](LICENSE). Upstream history, copyright notices and dependency
licenses are retained. Binary distributions must include access to corresponding
source, recursively pinned dependencies and build scripts.
