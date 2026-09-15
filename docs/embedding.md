# Embedding

The `embedded` Android library targets API 34 and later. It consists of the
native Termux:X11 server/renderer, its command entry point, a Binder bootstrap
interface and `X11Session`. It has no dependency on the standalone Activity,
preferences UI, companion loader APK, Android Desktop, HOME or privileged
display/window management. Android 14 device coverage is still required.

## Ownership

- One X server process per X session. Different sessions have independent
  `DISPLAY` sockets, clients, clipboard state and input focus.
- One `X11Session` per server connection. It owns its HandlerThread, socket,
  shared state, renderer, buffer registry and the single Present-queue consumer.
- Any number of dynamically identified `Output` objects per connection. XID
  zero selects the complete X screen. Other XIDs select Composite pixmaps;
  these are not cropped screenshots of the root window.
- `Output.setSurface` attaches or releases an Android surface. Closing an
  output does not close its X client. Closing a connection does not request
  server termination. The embedding application decides session retention.
- `ICmdEntryInterface.retain` binds an explicitly owned server to an application
  Binder. Owner death and `stop` request normal X server shutdown, including
  socket cleanup. They do not call Android window-management APIs.

Geometry is applied before frame pixmaps are sampled. Surface replacement is
acknowledged by the render thread before the host may destroy its Surface.
EGL initialization failure is reported to the caller instead of leaving the
surface acknowledgement blocked. EGL contexts and surface backing objects are
released per renderer; the process-wide EGLDisplay is not terminated by a
single session.

## Bootstrap

The embedding application registers an exported receiver for
`com.termux.x11.CmdEntryPoint.ACTION_START` before launching the server. It
generates a fresh unpredictable token for each pending session and launches
`app_process` with the embedding APK as `CLASSPATH` and these environment values:

| Variable | Meaning |
| --- | --- |
| `MAGICDESK_X11_PACKAGE` | Exact receiver application package |
| `MAGICDESK_X11_SESSION` | Host-owned session identity |
| `MAGICDESK_X11_TOKEN` | Pending-session authentication token |
| `MAGICDESK_X11_LIBRARY` | Host's extracted `nativeLibraryDir/libXlorie.so` |
| `MAGICDESK_X11_OWNER_REQUIRED` | Require an authenticated owner before starting Xorg |
| `TMPDIR` | X socket/runtime directory in the selected Termux/container environment |
| `XKB_CONFIG_ROOT` | Keyboard configuration files from that environment |

The entry class is `com.termux.x11.CmdEntryPoint`. Use a distinct X display
number per server, or `-displayfd 1` to let Xorg atomically allocate a free one.
Do not inject the container's `LD_LIBRARY_PATH` into the
Android runtime; preserve the client's loader environment through upstream
`XSTARTUP_LD_LIBRARY_PATH` and `XSTARTUP_LD_PRELOAD` when needed.

Each broadcast contains `session`, `token`, `phase` and the command Binder in
the existing null-key Bundle. With `MAGICDESK_X11_OWNER_REQUIRED`, the server
first sends `phase=attach` using Android 14's `setShareIdentityEnabled(true)`.
Validate `BroadcastReceiver.getSentFromUid()` against the selected Termux UID,
the pending identity and token *before* using that Binder. Call `retain` with
the host's lifetime Binder. Only then does Xorg start. Its `phase=ready`
broadcast supplies the atomically allocated numeric `display`; verify that it
came from the same server before obtaining `getXConnection()` and passing its
descriptor to `X11Session.connect`. The embedded path does not use the
standalone app's fixed knock port or repeating readiness broadcasts. Tokens
must not be placed in logs or persistent user settings. A production launcher
must also cancel pending launches when its startup owner disappears.

An absent or cancelled admission is rejected before sockets are created. The
server exits if no owner attaches within 60 seconds. A stop received during
native initialization is applied when the server becomes ready. The host also
owns its startup deadline and rejects late callbacks; server termination uses
normal Xorg cleanup. The independent example can retain a server at readiness,
but a product host must use the pre-start ownership handshake.

Callbacks are delivered on the caller-supplied Executor. `onFrame` publishes
changes to dimensions/availability, not every animation frame. Mouse positions
are normalized to the displayed content, excluding letterboxing. Android key
events and Unicode text have separate entry points. Surface management is
acknowledged synchronously; call it from the host's lifecycle adapter, not an
unrelated privileged service worker.

## Build and Example

Initialize all submodules recursively. Build the independent example with
JDK 17+, Android SDK 37, NDK 27.3.13750724, CMake, Ninja, Python 3, Bison and
`patch` installed, plus a host C compiler for source generation. Windows uses
MSYS2 Bison/patch and a UCRT64 host compiler alongside the Android NDK. Add the
actual MSYS2 `usr/bin` and `ucrt64/bin` directories to PATH; Android native code
is still compiled by the NDK, not the host compiler:

```sh
./gradlew -p examples/android :app:assembleDebug :app:lintDebug :embedded:lintDebug
```

The SDK is selected through `ANDROID_HOME` or
`examples/android/local.properties`. In arm64 Termux the module uses the native
Clang/CMake toolchain and system Android EGL/GLES libraries, without a Linux
container. Pass `-Pandroid.aapt2FromMavenOverride="$PREFIX/bin/aapt2"` when needed.
The NDK path also builds x86_64; Termux builds only its native arm64 ABI.
Generated artifacts and local configuration are ignored by Git.

`ExampleActivity` is a test harness, not a product launcher. Start it with a
fresh `token` extra (at least 24 characters). Its `leftSession` and
`rightSession` extras select two independent connections, or the same
connection when equal. `leftWindow` and `rightWindow` are string XIDs; omitted
values select the whole X screen. Launch matching servers with the token and
session identities. The example's Binder owners terminate these test servers
when the example process dies.

Re-deliver an Intent using `FLAG_ACTIVITY_SINGLE_TOP`, the same `token`, and
`session`, plus `command=reconnect`, `recreate` or `stop` to exercise connection
replacement, output replacement and server shutdown. The example retains its
command interface while a connection is replaced. It deliberately closes its
servers on Activity destruction; a product host must retain sessions outside
its Activity if it wants configuration-change persistence.

Native fixtures:

```sh
mkdir -p build
clang examples/animated-windows.c -o build/animated-windows -lxcb -lxcb-composite
clang examples/control-window.c -o build/control-window -lxcb
DISPLAY=:43 build/animated-windows
DISPLAY=:43 build/control-window XID unmap
DISPLAY=:43 build/control-window XID map
```

`ahb-windows.c` exercises the upstream DRI3 AHardwareBuffer transport and GPU
Present copies. In arm64 Termux build it against system EGL, not Mesa EGL:

```sh
clang --target=aarch64-linux-android34 -fno-termux-rpath \
  -Wl,-rpath,/system/lib64:$PREFIX/lib examples/ahb-windows.c -o build/ahb-windows \
  -lxcb -lxcb-composite -lxcb-dri3 -lxcb-present \
  /system/lib64/libEGL.so /system/lib64/libGLESv2.so -landroid
DISPLAY=:44 X11_EXAMPLE_ROOT=1 build/ahb-windows
```

The example fixtures alone use permissive local X authentication (`-ac`) for
controlled tests. A product launcher needs its own Xauthority policy. Do not
expose the X server's TCP listener; local/container socket ownership is separate
from the Android Binder bootstrap token.

## Remaining Host Integration

This module is not an X window manager or an application catalog. Integration
still needs managed X client discovery, top-level/transient/menu relationships,
window-manager policy, clipboard/IME adapters, X cursor presentation and
application/container launch lifecycle. The host owns Android task placement
and must not conflate an Android display ID, output ID and XID. Only one owner
should request whole-screen geometry when multiple outputs select XID zero.

The current GPU path uses upstream AHardwareBuffer support. Imported raw
DMA-BUFs can still take upstream's CPU Present-copy fallback. No CPU screenshot
loop, Mesa replacement or firmware-specific GPU extension is added here.
