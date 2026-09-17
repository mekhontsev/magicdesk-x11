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
  zero selects the complete X screen. Other XIDs select Composite window families;
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
| `MAGICDESK_X11_XSETTINGS` | `1` enables the embedded XSettings manager for dedicated application sessions |
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
broadcast is sent from `ddxReady`, after Xorg allocates its display and
initializes sockets/screens/input. It supplies the numeric `display`; verify that it
came from the same server before obtaining `getXConnection()` and passing its
descriptor to `X11Session.connect`. The embedded path does not use the
standalone app's fixed knock port or repeating readiness broadcasts. Tokens
must not be placed in logs or persistent user settings. A production launcher
must also cancel pending launches when its startup owner disappears.
Use `-noreset` for retained sessions so the last X client leaving does not
reinitialize the server generation. Embedded client startup belongs to the
host, not the standalone `TERMUX_X11_XSTARTUP` preference. Embedded launchers
also supply their selected execution environment; the standalone Termux
package's preload library must not leak into that environment.

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
container. Install `vulkan-headers` for compilation; other build hosts use the
NDK headers. Vulkan itself is loaded dynamically from Android, not bundled or
required at startup. Pass `-Pandroid.aapt2FromMavenOverride="$PREFIX/bin/aapt2"` when needed.
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

## Window and Clipboard Contract

### Native Transport and Lifetime

The GUI command stream has one writer (the session handler thread). Renderer GPU
completion uses a separate, nonblocking `eventfd`, passed during connection setup
and observed on the X server thread. It must never write into the command stream,
including between a command header and its ancillary FD. The server unregisters
and closes its completion FD on disconnect, replacement and shutdown.

Buffer transport carries fixed-width metadata and an FD or AHardwareBuffer handle,
not a runtime `LorieBuffer` struct. The receiver owns fresh reference counts, locks,
list links and GL objects. FD imports validate format, dimensions, stride, extent
and overflow; pixel-aligned offsets need not be page-aligned. Mapping ownership is
separate from the pixel pointer, and the last row need not include trailing padding.
An incomplete or invalid buffer message terminates the connection, not a partial
registration. Server and embedded library must be built from the same revision.

X server work has one FIFO across all producers. Callbacks execute outside its
mutex; newly queued work and unsuccessful callbacks wait for the next pass.
Zombie-client cleanup preserves the order of surviving work and is reentrant.

Run `sh scripts/verify-native.sh` on Linux or Termux. Portable tests cover queue
ordering/reentrancy, GPU notification isolation and buffer layout/Present sizing.
Termux additionally tests the actual buffer implementation with fragmented IPC,
FD cleanup, nonzero offsets and injected AHardwareBuffer lock failures. These are
native fixtures, independent of Android Desktop self-tests.

### Window Discovery

`onWindowsChanged` publishes complete immutable snapshots after reconciliation,
never intermediate removals from a batch. Discovery follows X properties and
screen/property callbacks, without polling. A newly discovered application
must have been mapped at least once. Unmapped known clients remain in the
catalog until their resource is destroyed or ceases to be an application.
Desktop, dock, splash, override-redirect and explicitly transient windows are
excluded from the application catalog. Titles are bounded to 255 UTF-8 bytes.

An individual output composites its main pixmap and the mapped transient
family, back to front, with premultiplied alpha for depth-32 layers. Group
transients/unparented popups follow the focused member only when their
`WM_CLIENT_LEADER` agrees. Explicit transient chains are bounded against
cycles. Ordinary children remain part of their parent pixmap. The server
shares Composite/Damage ownership for repeated XIDs; each output's layers
and frame arrive as one committed presentation. Families are bounded to 256
layers and clipped to the main viewport.

`Output.focus` preserves active keyboard grabs and existing dialog focus,
otherwise respecting `WM_HINTS` and `WM_TAKE_FOCUS`. `closeWindow` sends
`WM_DELETE_WINDOW`, falling back to X client termination if unsupported.
Output destruction itself still releases only a presentation. The host
decides when to close clients and retained/application sessions.

The focused host enables clipboard exchange through `X11Session.dataExchange()`.
`X11DataExchange.Source` publishes MIME target names and opens independently
owned seekable descriptors on demand. `Offer.open()` must run on a worker, never
the renderer or callback thread. Clipboard and XDND have separate selections,
generation IDs and lifetimes. The native engine handles TARGETS, TIMESTAMP and
INCR with a 128 MiB per-transfer limit, 16 pending requests and 30-second protocol
deadlines. It does not access Android's ClipboardManager or content providers.

Android drag hosts publish a DRAG offer, then send ENTER/MOVE/LEAVE/DROP in output
coordinates. Drop waits for target acceptance and reports FINISH. An X11 source
offer includes its output identity: the host can start Android drag-and-drop,
send BEGIN and report FINISH/CANCEL. An X-side destination retains the toolkit's
pointer-grab/drop handshake while Android owns the gesture. Copy is the supported
action. Same-server drops retain the original XdndSelection; cross-server drops
use host-provided data. Source disappearance, timeout and disconnect cancel work.

The retained Binder owner can open local regular files and import content FDs
through `ICmdEntryInterface`. Access uses the server process UID, never an elevated
host identity. For file imports the launcher supplies a fresh private
`MAGICDESK_X11_CONTENT_DIR` and removes it after server exit. Imports survive
individual drag completion, with a 256 MiB/128-file session bound. Namespace
translation for containers is not implicit. Host-managed Android URI grants,
exports and clipboard-origin metadata remain outside this module.

The host owns Android task placement, application discovery, IME and session
lifecycle. It must not conflate an Android display ID, output ID and XID. Only
one owner should request whole-screen geometry when multiple outputs select
XID zero. Custom X cursor presentation and out-of-viewport popup placement
remain host integration work.

`examples/content-window.c` is a GTK clipboard/XDND fixture for text, HTML, PNG
and files, including a 700,000-byte INCR text selection. Build it with
`cc examples/content-window.c -o content-window $(pkg-config --cflags --libs gtk+-3.0)`
and launch it inside independent sessions. Receipts report byte counts and hashes;
file drops also check readability in the receiving environment. No test client
dependency is linked into the Android library.

## Hosted Window Input

Individual outputs reconcile selected-window geometry after X position changes
and Android Surface resizing. Negative saved positions are brought into the
root's input area, moving the containing top-level frame when necessary, and
the root grows to contain positive extents. Composite pixels and pointer hit
testing therefore refer to reachable X coordinates. XID-zero outputs do not
reposition clients; a whole Linux desktop retains its own window manager.
An individual XID keeps the last size requested by its Android Surface even
after client ConfigureWindow requests. If several outputs select the same XID,
only the most recently resized output owns its size; other outputs fit the
result with preserved aspect ratio. Releasing that owner transfers size control
to a remaining sized output. This is event-driven reconciliation, not polling.

`examples/input-window.c` creates a client at a negative saved position and logs
ConfigureNotify and real pointer events. Open it as an individual output, verify
that clicks arrive, then use `control-window ... geometry` to move it off-screen
again and verify reconciliation without an Android resize. In a whole-desktop
output the same initial position must remain untouched.
Also request a different client size and verify it returns to the hosted size;
repeat with two outputs and release the size owner to check there is no resize
feedback loop. The fixture publishes a four-color `_NET_WM_ICON` for verifying
Android task presentation independently of an installed application's icons.

## Window Icons

Catalog snapshots carry optional client `_NET_WM_ICON` images. The native parser
validates the EWMH CARDINAL stream (at most one million values), chooses the
closest suitable size and fits it into 64x64 ARGB pixels. Invalid or missing
properties clear the icon. Pixel data follows the window-info header only when
present; socket reads retain fragmented payloads before committing the catalog.
Input and frame headers do not grow with the icon payload. The Java adapter
publishes immutable Bitmap values and reuses unchanged icons.

`examples/window-icon-test.c` exercises selection, aspect preservation, alpha,
truncated properties, invalid sizes and allocation limits with a host C compiler.

## Logical Density

Pass the initial X11 density using Xorg's `-dpi`; `X11Session.setDpi` updates it
through the existing server command stream. The host selects density ownership
when several Android windows share one X screen. The library does not inspect
Android displays, poll tasks or scale captured pixels.

Embedded startup publishes `Xft.dpi` in the root resource database. Updates
retain ownership only while that database is unchanged. Dedicated application
sessions additionally own an unmapped InputOnly XSettings window with atomic
`Xft/DPI`, `Gdk/WindowScalingFactor` and `Gdk/UnscaledDPI` settings. The GTK integer
scale is the nearest positive integer; font DPI retains the fractional remainder.
A replacement XSettings owner destroys the old manager window and is never
displaced. Whole-desktop sessions should leave this manager disabled so their
Linux settings daemon can own the selection normally.

Density commands update RandR millimetres and send normal setting/geometry
notifications. Resizing retains density on both axes. They do not reallocate
the screen pixmap solely for a DPI change. Bounds and input stay in X pixels.
Toolkit-specific environment overrides remain application-owned.
`examples/density-settings-test.c` checks the fixed, bounded XSettings wire
format and the integer-widget/fractional-font conversion.

`examples/density-window.c` is a GTK 3 client for live density and input checks.
It reports toolkit scaling changes and retains its button state across resizes.
Build in Termux with the GTK 3 development files available, then run the binary
as a session command:

```sh
clang examples/density-window.c -o build/density-window $(pkg-config --cflags --libs gtk+-3.0)
```

## Optional DMA Copy

AHardwareBuffer sources retain upstream's deferred EGL Present queue. Linear
DRI3 DMA-BUF sources instead use an optional synchronous EXA copy accelerator:
the system Vulkan driver imports the source allocation and destination AHB.
The existing renderer lock and GPU completion fence protect pixmap reuse and
presentation. Imports belong to pixmap lifetimes, not individual frames.

`dma_copy.c` owns Vulkan loading, capability checks, imports and transfer
commands. It has no link-time Vulkan dependency. Unsupported drivers/buffers
return to the normal EXA CPU implementation. An ordinary shared-memory FD is
not a DMA-BUF and is rejected before entering Vulkan. Failed device operations
quiesce the queue and disable subsequent Vulkan copies in that context. If
completion cannot be established, the isolated X server terminates rather than
racing the GPU with a CPU fallback or reusing a client buffer still in flight.

Some drivers require trailing buffer allocation space absent from the DRI3
image. The accelerator imports the fitting whole rows and stages the remaining
suffix; it never binds an undersized allocation. This may retain a small CPU
upload and does not promise universal zero-copy. No CPU screenshot loop, Mesa
replacement, private gralloc handle or firmware-specific extension is used.
`MAGICDESK_X11_DISABLE_VULKAN_COPY=1` in the server environment disables this
accelerator for comparisons; default selection is automatic.

The native fixture compares full/partial copies and source offsets pixel for
pixel, preserves unaffected destination pixels, checks rejection of ordinary
shared memory and simulates an unavailable Vulkan loader. In Termux:

```sh
clang --target=aarch64-linux-android34 examples/dma-copy-producer.c \
  -o build/dma-copy-producer -lvulkan
clang --target=aarch64-linux-android34 -fno-termux-rpath \
  -Ilorie/src/main/cpp/lorie -Wl,-rpath,/system/lib64 -Wl,--wrap=dlopen \
  examples/dma-copy-test.c lorie/src/main/cpp/lorie/dma_copy.c \
  -o build/dma-copy-test /system/lib64/libandroid.so -llog -ldl
build/dma-copy-test build/dma-copy-producer
```

The producer intentionally uses Termux's Vulkan loader/Mesa; the copy backend
uses Android's system loader. Run this positive import test only on a device
with the required external-memory extensions. Unsupported production devices
must still render through the CPU fallback.
