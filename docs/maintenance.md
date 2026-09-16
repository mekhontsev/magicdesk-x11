# Maintenance

`main` contains the MagicDesk embedding work. `upstream/master` is the original
Termux:X11 history. The initial base is
`53f8437326dbbe26b3756480aece7f604d2c1900`. Do not squash or copy the upstream
tree into a new unrelated repository. Keep embedded-only classes in `embedded`,
and keep native output/session responsibilities in focused source files.

```sh
git remote add upstream https://github.com/termux/termux-x11.git
git fetch upstream
git switch main
git merge upstream/master
git submodule update --init --recursive
```

Resolve shared protocol, rendering and build changes explicitly. One server
connection must keep one Present-queue consumer even when several outputs are
visible. Compare against the upstream standalone path before attributing a GPU
fallback to the embedding work. Preserve original copyright/license notices.

Run the example build and Lint, then verify complete X-screen output, two
window outputs, two simultaneous servers, resizing, map/unmap, input isolation,
connection/output recreation and owner-death/normal-shutdown cleanup. Exercise
both ordinary pixmaps and the AHardwareBuffer Present fixture. Build checks
alone do not establish support on a new Android release or graphics driver.

Also exercise clipboard and XDND with `examples/content-window.c`: text, HTML,
PNG, readable file paths, large INCR selections and same-/cross-server drops.
Verify source loss, cancellation, bounded transfers and owner-death cleanup.
Run Android content-grant and clipboard-focus workflows in the embedding host;
the example alone cannot verify its authorization policy. Geometry, icon and
density fixtures are described in [Embedding](embedding.md).

CI packages corresponding source with initialized submodules before native
configuration: upstream CMake applies tracked patches inside submodule
worktrees. The source archive therefore contains pinned upstream sources and
the recipes which apply those patches, rather than silently recording a
different submodule revision. Binaries retain component license notices as
Android assets. Never commit APKs, signing keys, local logs or bootstrap tokens.
