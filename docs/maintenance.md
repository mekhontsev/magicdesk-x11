# Maintenance

The working branch contains the native engine only. Preserve the original
Termux:X11 ancestry (initial base 53f8437326dbbe26b3756480aece7f604d2c1900).
Do not squash it into an unrelated repository. Native dependency paths remain
unchanged to keep shared rendering, protocol and build changes reviewable.

```sh
git remote add upstream https://github.com/termux/termux-x11.git
git fetch upstream
git switch main
git merge upstream/master
git submodule update --init --recursive
```

Add the upstream remote only once. Its remote-tracking branch retains the full
upstream tree independently of our working branch. Resolve modify/delete
conflicts deliberately: do not restore Java, AIDL, Android application resources,
Gradle modules or the old standalone JNI/clipboard/viewport path. Port necessary
native changes into the retained engine. Keep copyright/license notices.

The public embedded.h contract has opaque connections and native callbacks;
it must not name a Java class, host package or Binder interface. Android lifecycle,
authorization and JNI changes belong in MagicDesk, which pins this native revision.
Change both sides together when modifying the native contract; no compatibility
layer for older MagicDesk builds is required.

Run scripts/verify-native.sh and link lorie-smoke for both supported Android ABIs.
The fork's CI checks Linux and Windows NDK builds without Java or Gradle.
Then run MagicDesk's Java tests, build/Lint and Android example. Verify complete
screen output, two window outputs, independent servers, input, resize, reconnect,
output recreation, normal shutdown and owner-death cleanup. Exercise AHB Present
and ordinary pixmaps, clipboard/XDND, large INCR and transfer cancellation.
Build checks alone do not establish support on a new Android release or driver.

Package corresponding source before native configuration: CMake applies tracked
patches inside pinned dependency worktrees. The distributable includes original
dependency revisions and the recipes applying those patches. MagicDesk packages
component notices as assets. Never commit binaries, local reports or session tokens.
