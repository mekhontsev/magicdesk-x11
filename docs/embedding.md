# Native Embedding

The LorieNative CMake target supplies the X server and renderer through
`lorie/embedded.h`. No JVM, Java classes, Binder authorization, Android Activity
or standalone Termux:X11 loader is part of the engine. The supported native
Android floor is API 34; device coverage remains a separate obligation.

## Ownership And Threads

One X server runs per isolated process. Call `lorieServerStart` on a prepared
Android Looper with explicit arguments and accessible TMPDIR/XKB_CONFIG_ROOT.
The engine copies arguments and starts its X thread. Its ready callback runs
on that thread at ddxReady, after sockets and input are initialized. Only then
may the host connect or request normal shutdown. Server exit ends that isolated
process. Host authorization and start/stop races must be resolved by the host.

One opaque LorieConnection owns a renderer and the single Present consumer for
its server. Create, connect, command, surface and destroy calls are serialized
on the creating Looper. Native callbacks run on that same thread and borrow
their string/icon memory only for their duration. A data callback owns its FD.
No per-frame callback into Java is required. The host supplies any JVM adapter.

`embedded.h` exposes named output/input/window commands and structured window
snapshots. `output_commands.cpp` alone translates their arguments to the private
`output_command.h` wire representation. Commands use stack values and the existing
connection FIFO, with no extra native heap allocation or per-command thread.
Normalized pointer coordinates are clipped and quantized at this boundary.
Platform keycode translation belongs to the host adapter, not the X server.

Each output selects XID zero (the whole screen) or a Composite window family.
Surface calls borrow ANativeWindow; the renderer retains its own reference and
acknowledges replacement before the host can release its surface. Releasing an
output does not close an X client. Destroying a connection does not stop Xorg.
The host decides when to close clients and retained sessions.

An API-34 native ImageReader keeps EGL current while outputs are absent. Renderer
threads never attach to a JVM. All outputs, including whole-screen output, use
the same presentation path. EGL contexts are per connection; destroying one
does not terminate the process-wide EGLDisplay used by another.

## Build And Fixtures

See the root README for NDK/Windows builds. Termux uses Clang/CMake, the Android
system EGL/GLES libraries and vulkan-headers. The optional Vulkan path loads the
system driver dynamically and is never a startup requirement.

`lorie-smoke` is a native-only test host: it starts a server, obtains/closes a
renderer socket at readiness, and requests normal shutdown. Run it with explicit
TMPDIR/XKB_CONFIG_ROOT and Xorg arguments, for example `-displayfd 1 -noreset
-nolisten tcp -ac` in an isolated test environment. Production authentication
belongs to the host. Android lifecycle tests live in MagicDesk's x11-runtime/example.

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

The GUI command stream has one writer (the session handler thread). Commands
use a bounded, nonblocking FIFO (1 MiB / 4096 messages), drained on socket
writability. Headers and independently retained ancillary FDs preserve ordering
through partial writes. Overflow or transport failure disconnects the session;
commands are never silently discarded and the Looper never waits for a reader.
Renderer GPU
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
registration. Server and renderer must be built from the same revision.

X server work has one FIFO across all producers. Callbacks execute outside its
mutex; newly queued work and unsuccessful callbacks wait for the next pass.
Zombie-client cleanup preserves the order of surviving work and is reentrant.

The shared pixel mutex protects CPU/GPU access through GPU completion, not socket
publication or `eglSwapBuffers`. Each caller supplies its own peer-liveness context;
renderers must never consult the X server's process-global socket. Monotonic timed
acquisition observes peer loss without imposing a GPU-operation deadline. A broken
peer fails that session; a live or abandoned mutex is never reinitialized in place.
EXA FinishAccess releases the acquisition recorded by PrepareAccess. The renderer
snapshots output metadata under its local lock, then draws without holding it.
Only the render thread replaces EGL surfaces or releases GL buffers, so snapshots
retain their resources until the next iteration. Disconnect shuts down the socket
before waiting for shared-state/Surface detachment, allowing lock waits to cancel.

Run `sh scripts/verify-native.sh` on Linux or Termux. Portable tests cover queue
ordering/reentrancy, GPU notification isolation and buffer layout/Present sizing.
They also cover contended/recursive shared locks, peer death, command backpressure,
partial writes and queued-FD lifetime.
`examples/output-commands-test.cpp` exercises the production command encoder:
operation order, output/window IDs, pointer clipping/rounding, key/text commands
and unsigned fullscreen request serials retain their existing wire semantics.
Termux additionally tests the actual buffer implementation with fragmented IPC,
FD cleanup, nonzero offsets and injected AHardwareBuffer lock failures. These are
native fixtures, independent of Android Desktop self-tests.

### Window Discovery

`MAGICDESK_X11_HOST_WM=1` enables the dedicated-application EWMH bridge before
client startup. It owns `WM_S0`, publishes `_NET_SUPPORTING_WM_CHECK` and only
the supported state hints, and yields if another window manager takes the
selection. Leave it disabled for whole Linux desktops. The native bridge owns
X protocol, not Android task policy.

Window callbacks supply `LorieWindowInfo` metadata and `LorieWindowManagement`,
which separates management authority, the versioned `LorieWindowRequest`, and
confirmed `LorieWindowState`. A null snapshot removes the given XID. Snapshot
storage, titles and icon pixels are borrowed only during the callback.
Initial fullscreen hints and `_NET_WM_STATE` add/remove/toggle messages create
requests. `lorieConfirmWindowState` accepts the XID, request serial and actual
state; only the private wire encoder packs these into command fields. Only the current serial may
update `_NET_WM_STATE`; unrelated atoms are retained. Serials are allocated
across the session to reject replies to destroyed/reused windows. Renderer
reconnection republishes state without forgetting client intent. A host must
select one responder when multiple outputs view an XID. Window geometry still
comes from the host Surface, never from an X-side fullscreen resize guess.

`examples/fullscreen-state-test.c` covers pending toggles, stale replies,
rejection and manual restore. `control-window XID fullscreen 0|1|2` sends the
real EWMH client message for end-to-end checks.

The window callbacks followed by `windowsCommitted` publish complete snapshots after reconciliation,
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

`lorieOutputFocus` preserves active keyboard grabs and existing dialog focus,
otherwise respecting `WM_HINTS` and `WM_TAKE_FOCUS`. `lorieCloseWindow` sends
`WM_DELETE_WINDOW`, falling back to X client termination if unsupported.
Output destruction itself still releases only a presentation. The host
decides when to close clients and retained/application sessions.

The host enables exchange and publishes native data offers through `lorieConnectionData`.
Descriptors are independently owned by the receiving callback. Clipboard and XDND have separate selections,
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

Resolving file paths and importing files belongs to the host adapter running in
the server execution domain. This native module does not open Android providers
or invoke a privileged file service.

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
Input and frame headers do not grow with the icon payload. The host's window
callback borrows icon pixels only until it returns.

`examples/window-icon-test.c` exercises selection, aspect preservation, alpha,
truncated properties, invalid sizes and allocation limits with a host C compiler.

## Logical Density

Pass the initial X11 density using Xorg's `-dpi`; `lorieSetScreenDpi` updates it
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
