#!/usr/bin/env bash
# The contents of this file are subject to the Common Public Attribution
# License Version 1.0 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# https://opensource.org/license/cpal-1-0. The License is based on the
# Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
# to cover use of software over a computer network and provide for limited
# attribution for the Original Developer. In addition, Exhibit A has been
# modified to be consistent with Exhibit B.
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Fluxion Engine.
#
# The Original Developer is not the Initial Developer and is __________. If
# left blank, the Original Developer is the Initial Developer.
#
# The Initial Developer of the Original Code is Kiss Tibor Péter. All
# portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
# All Rights Reserved.
#
# Contributor ______________________.
#
# Alternatively, the contents of this file may be used under the terms of
# the Fluxion Engine Commercial License Agreement Version 1.0, separately
# obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
# License"), in which case the provisions of the Commercial License are
# applicable instead of those above.
#
# If you wish to allow use of your version of this file only under the terms
# of the Commercial License and not to allow others to use your version of
# this file under the CPAL, indicate your decision by deleting the
# provisions above and replace them with the notice and other provisions
# required by the Commercial License. If you do not delete the provisions
# above, a recipient may use your version of this file under either the CPAL
# or the Commercial License.
#
# SPDX-License-Identifier: CPAL-1.0

# Building and testing on Linux (including WSL), fast.
#
#   Tools/linux.sh setup                install what building needs (apt)
#   Tools/linux.sh build [debug|release|asan|all]
#   Tools/linux.sh test  [ctest args]   debug build + all tests
#   Tools/linux.sh gate                 debug + release + asan, all tests
#   Tools/linux.sh watch [ctest args]   re-test whenever a file changes
#
# When the repository sits on a Windows disk (WSL's /mnt/...), sources are
# mirrored to the Linux disk first and built there: the /mnt translation
# layer makes even a DO-NOTHING configure-plus-build cost a minute, while
# the whole mirrored cycle -- sync, build, every test -- runs in seconds.
# The mirror is one-way; edit on the Windows side, always.
#
# ccache is used when present; a clean rebuild then costs ~2 seconds
# instead of ~40.

set -e

SOURCE="$(cd "$(dirname "$0")/.." && pwd)"
case "$SOURCE" in
    /mnt/*)
        MIRROR="$HOME/fluxion/src"
        NEED_SYNC=1
        ;;
    *)
        MIRROR="$SOURCE"
        NEED_SYNC=0
        ;;
esac
BUILDS="$HOME/fluxion/build"

COMMAND="${1:-test}"
shift || true

LAUNCHER_ARGS=()
if command -v ccache > /dev/null; then
    LAUNCHER_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

sync_sources() {
    if [ "$NEED_SYNC" = "1" ]; then
        mkdir -p "$MIRROR"
        rsync -a --delete --exclude=build/ --exclude=.git/ --exclude="*.pipelinecache" "$SOURCE/" "$MIRROR/"
    fi
}

configure_if_missing() {
    if [ ! -f "$1/CMakeCache.txt" ]; then
        local tree="$1"
        shift
        cmake -S "$MIRROR" -B "$tree" -G Ninja "${LAUNCHER_ARGS[@]}" "$@" > /dev/null
    fi
}

tree_for() {
    case "$1" in
        debug)   echo "$BUILDS/gcc" ;;
        release) echo "$BUILDS/release" ;;
        asan)    echo "$BUILDS/asan" ;;
    esac
}

configure_for() {
    case "$1" in
        debug)   configure_if_missing "$BUILDS/gcc" -DCMAKE_BUILD_TYPE=Debug ;;
        release) configure_if_missing "$BUILDS/release" -DCMAKE_BUILD_TYPE=Release ;;
        asan)    configure_if_missing "$BUILDS/asan" -DCMAKE_BUILD_TYPE=Debug \
                     -DCMAKE_C_FLAGS="-fsanitize=address,leak -fno-omit-frame-pointer" \
                     -DCMAKE_CXX_FLAGS="-fsanitize=address,leak -fno-omit-frame-pointer" \
                     -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,leak" ;;
    esac
}

build_one() {
    configure_for "$1"
    cmake --build "$(tree_for "$1")" -j "$(nproc)"
}

test_one() {
    local config="$1"
    shift
    build_one "$config"
    (cd "$(tree_for "$config")" && ctest -j "$(nproc)" --output-on-failure "$@")
}

case "$COMMAND" in
    setup)
        # Everything a build and the tests reach for. vulkan-tools brings
        # a software Vulkan device, which is what lets the GPU tests run
        # on a machine with no GPU at all.
        sudo apt-get update
        sudo apt-get install -y build-essential g++ cmake ninja-build ccache rsync \
            libx11-dev libxrandr-dev libvulkan-dev vulkan-tools mesa-vulkan-drivers
        ;;

    build)
        sync_sources
        CONFIG="${1:-debug}"
        if [ "$CONFIG" = "all" ]; then
            build_one debug
            build_one release
            build_one asan
        else
            build_one "$CONFIG"
        fi
        ;;

    test)
        sync_sources
        test_one debug "$@"
        ;;

    gate)
        sync_sources
        test_one debug
        test_one release
        test_one asan
        ;;

    watch)
        # Polling, not inotify: file-change events do not cross the /mnt
        # translation layer, so watching there would see nothing at all.
        # rsync itself is the change detector -- it prints what it moved.
        echo "watching for changes; Ctrl-C stops"
        while true; do
            if [ "$NEED_SYNC" = "1" ]; then
                mkdir -p "$MIRROR"
                CHANGED="$(rsync -ai --delete --exclude=build/ --exclude=.git/ --exclude="*.pipelinecache" "$SOURCE/" "$MIRROR/" | head -1)"
                if [ -n "$CHANGED" ]; then
                    test_one debug "$@" || true
                    echo "--- watching ---"
                fi
            else
                test_one debug "$@" || true
                echo "--- watching (no mirror: re-running each cycle) ---"
            fi
            sleep 2
        done
        ;;

    *)
        echo "Commands: setup | build [debug|release|asan|all] | test [ctest args] | gate | watch [ctest args]"
        ;;
esac
