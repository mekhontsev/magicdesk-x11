#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
src=$root/lorie/src/main/cpp
work=$(mktemp -d)
trap 'rm -rf -- "$work"' 0
cc=${CC:-clang}
cxx=${CXX:-clang++}
android_target=
if [ "$(uname -o)" = Android ]; then android_target='--target=aarch64-linux-android34 -L/system/lib64'; fi
# Queue tests need pixman declarations only; no pixman code or version feature is used.
sed 's/@PIXMAN_VERSION_[A-Z]*@/0/g' "$src/pixman/pixman/pixman-version.h.in" > "$work/pixman-version.h"
"$cc" -std=c11 -O2 -Wall -Wextra -UNDEBUG "$root/examples/buffer-layout-test.c" -o "$work/layout"
"$cc" -std=c11 -O2 -Wall -Wextra -UNDEBUG -pthread "$root/examples/gpu-completion-test.c" -o "$work/gpu"
"$cxx" -std=c++17 -O2 -Wall -Wextra -UNDEBUG -pthread \
    -I"$work" -I"$root/examples/host-config" -I"$src/xserver/include" -I"$src/xserver/Xext" \
    -I"$src/xserver/Xi" -I"$src/xorgproto/include" -I"$src/pixman/pixman" \
    -I"$src/libxfont/include" "$root/examples/workqueue-test.cpp" -o "$work/queue"
timeout 15 "$work/layout"
timeout 15 "$work/gpu"
timeout 15 "$work/queue"
"$cc" $android_target -std=c11 -O2 -Wall -Wextra -UNDEBUG -pthread "$root/examples/shared-lock-test.c" -o "$work/lock"
"$cxx" -std=c++17 -O2 -Wall -Wextra -UNDEBUG "$root/examples/command-queue-test.cpp" -o "$work/commands"
timeout 15 "$work/lock"
timeout 15 "$work/commands"
"$cxx" -std=c++17 -O2 -Wall -Wextra -UNDEBUG "$root/examples/output-commands-test.cpp" \
    "$src/lorie/output_commands.cpp" -o "$work/output-commands"
timeout 15 "$work/output-commands"
"$cc" -std=c11 -O2 -Wall -Wextra -UNDEBUG "$root/examples/fullscreen-state-test.c" -o "$work/fullscreen"
timeout 15 "$work/fullscreen"
"$cc" -std=c11 -O2 -Wall -Wextra -UNDEBUG "$root/examples/window-placement-test.c" -o "$work/placement"
timeout 15 "$work/placement"
"$cc" -std=gnu11 -O2 -Wall -Wextra -UNDEBUG \
    -I"$work" -I"$root/examples/host-config" -I"$src/xserver/include" -I"$src/xserver/Xext" \
    -I"$src/xserver/Xi" -I"$src/xorgproto/include" -I"$src/pixman/pixman" \
    -I"$src/libxfont/include" -I"$src/xserver/miext/damage" -I"$src/xserver/render" \
    "$root/examples/window-family-test.c" -o "$work/family"
timeout 15 "$work/family"
"$cc" -std=c11 -O2 -Wall -Wextra -UNDEBUG "$root/examples/window-size-test.c" -o "$work/size"
timeout 15 "$work/size"
"$cc" -std=c11 -O2 -Wall -Wextra -UNDEBUG -I"$src/libxcvt/include" \
    "$root/examples/screen-mode-test.c" "$src/libxcvt/lib/libxcvt.c" -lm -o "$work/screen-mode"
timeout 15 "$work/screen-mode"

# Output ownership and buffer tests compile the implementation against Android's ABI.
if [ "$(uname -o)" = Android ]; then
    "$cc" $android_target -std=gnu11 -O2 -Wall -Wextra -UNDEBUG -ffunction-sections -fdata-sections \
        -I"$work" -I"$root/examples/host-config" -I"$src/xserver/include" -I"$src/xserver/Xext" \
        -I"$src/xserver/Xi" -I"$src/xorgproto/include" -I"$src/pixman/pixman" \
        -I"$src/libxfont/include" -I"$src/xserver/miext/damage" -I"$src/xserver/render" \
        -I"$src/xserver/composite" -I"$src/xserver/mi" -I"$src/xserver/damageext" -I"$src/xserver/xfixes" \
        "$root/examples/output-input-test.c" -Wl,--gc-sections -o "$work/output-input"
    timeout 15 "$work/output-input"
    "$cc" -std=gnu11 -DANDROID -D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ -DEGL_NO_PLATFORM_SPECIFIC_TYPES \
        -ffunction-sections -fdata-sections -Wno-nullability-completeness \
        -I"$work" -I"$src/xserver/include" -I"$src/pixman/pixman" \
        "$root/examples/buffer-transport-test.c" -Wl,--gc-sections,--no-as-needed \
        -L/system/lib64 -landroid -lEGL -lGLESv2 -lpixman-1 -o "$work/buffer"
    env -u LD_PRELOAD LD_LIBRARY_PATH=/system/lib64:$PREFIX/lib timeout 15 "$work/buffer"
fi
