#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
src=$root/lorie/src/main/cpp
work=$(mktemp -d)
trap 'rm -rf -- "$work"' 0
cc=${CC:-clang}
cxx=${CXX:-clang++}
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

# Buffer ownership and AHardwareBuffer failure injection use Android's actual ABI.
if [ "$(uname -o)" = Android ]; then
    "$cc" -std=gnu11 -DANDROID -D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ -DEGL_NO_PLATFORM_SPECIFIC_TYPES \
        -ffunction-sections -fdata-sections -Wno-nullability-completeness \
        -I"$work" -I"$src/xserver/include" -I"$src/pixman/pixman" \
        "$root/examples/buffer-transport-test.c" -Wl,--gc-sections,--no-as-needed \
        -L/system/lib64 -landroid -lEGL -lGLESv2 -lpixman-1 -o "$work/buffer"
    env -u LD_PRELOAD LD_LIBRARY_PATH=/system/lib64:$PREFIX/lib timeout 15 "$work/buffer"
fi
