#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
build="${BUILD_DIR:-$repo/build_core11_switch}"
source_args=()
donor="${FULLDEX_SOURCE:-$repo/../pokemon-fulldex}"
if [[ -d "$donor/melonDS" ]]; then source_args=("-DFULLDEX_SOURCE=$donor"); fi
cmake -S "$repo/core11" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" "${source_args[@]}"
cmake --build "$build" --target nro --parallel "${JOBS:-8}"
