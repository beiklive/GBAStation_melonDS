#!/usr/bin/env bash
# Build the Nintendo Switch NRO from an MSYS2 shell.
#
# Prerequisites:
#   - devkitPro/devkitA64 (DEVKITPRO and DEVKITA64)
#   - the devkitPro tools (elf2nro, nacptool and uam)
#   - CMake and make available in the MSYS2 environment
#
# Usage:
#   ./switchbuild.sh [-j JOBS] [--build-dir DIR] [--clean]

set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build_switch}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

# A relative BUILD_DIR is relative to the repository, not to the caller's cwd.
case "${BUILD_DIR}" in
    /*|[A-Za-z]:[\\/]*) ;;
    *) BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}" ;;
esac

usage() {
    cat <<EOF
Usage: $0 [-j JOBS] [--build-dir DIR] [--clean]

Build melonDS.nro using the Switch toolchain from an MSYS2 shell.

  -j, --jobs N       Number of parallel compile jobs (default: nproc, or 4).
      --build-dir D  CMake build directory (default: build_switch).
      --clean         Clean an existing CMake build before compiling.
  -h, --help         Show this help.

DEVKITPRO and DEVKITA64 may be set in the environment. DEVKITPRO defaults to
/opt/devkitpro and DEVKITA64 defaults to \$DEVKITPRO/devkitA64.
EOF
}

JOBS=""
CLEAN=0
while (( $# > 0 )); do
    case "$1" in
        -j|--jobs)
            (( $# >= 2 )) || { echo "ERROR: $1 requires a value" >&2; exit 2; }
            JOBS="$2"
            shift 2
            ;;
        --build-dir)
            (( $# >= 2 )) || { echo "ERROR: --build-dir requires a value" >&2; exit 2; }
            BUILD_DIR="$2"
            case "${BUILD_DIR}" in
                /*|[A-Za-z]:[\\/]*) ;;
                *) BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}" ;;
            esac
            shift 2
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --opengl)
            echo "ERROR: Switch OpenGL frontend is temporarily disabled." >&2
            exit 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "${JOBS}" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    else
        JOBS=4
    fi
fi
if [[ ! "${JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: jobs must be a positive integer (got '${JOBS}')" >&2
    exit 2
fi

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-${DEVKITPRO}/devkitA64}"
export PATH="${DEVKITPRO}/tools/bin:${DEVKITA64}/bin:${PATH:-}"

TOOLCHAIN_FILE="${ROOT_DIR}/cmake/Toolchain-cross-Switch.cmake"
[[ -f "${TOOLCHAIN_FILE}" ]] || {
    echo "ERROR: Switch toolchain file not found: ${TOOLCHAIN_FILE}" >&2
    exit 1
}

required_commands=(cmake aarch64-none-elf-g++ elf2nro nacptool uam)
for command_name in "${required_commands[@]}"; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "ERROR: '${command_name}' was not found in PATH." >&2
        echo "       Install devkitPro/MSYS2 packages and check DEVKITPRO (${DEVKITPRO})." >&2
        exit 1
    fi
done

if (( CLEAN )) && [[ -d "${BUILD_DIR}" ]]; then
    echo "Cleaning ${BUILD_DIR} ..."
    # A fresh directory also discards stale compiler dependency files from a
    # previous MSYS/Windows path configuration.
    rm -rf -- "${BUILD_DIR}"
fi

# Keep compiler/CMake temporary files inside the build tree. This avoids
# inherited Windows TEMP paths that may be inaccessible from an MSYS shell.
TMP_DIR="${BUILD_DIR}/.tmp"
mkdir -p "${TMP_DIR}"
export TMPDIR="${TMP_DIR}"
if command -v cygpath >/dev/null 2>&1; then
    TMP_WIN="$(cygpath -w "${TMP_DIR}")"
    export TMP="${TMP_WIN}"
    export TEMP="${TMP_WIN}"
else
    export TMP="${TMP_DIR}"
    export TEMP="${TMP_DIR}"
fi

echo "Configuring Switch build ..."
BUILD_TARGET="melonDS.nro"
OPENGL_ARGS=(-DBUILD_SWITCH_GL=OFF -DBUILD_SWITCH=ON -DENABLE_OGLRENDERER=OFF -DENABLE_DEKOGPU=ON)
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_DEPENDS_USE_COMPILER=FALSE \
    -DBUILD_QT_SDL=OFF \
    "${OPENGL_ARGS[@]}"

echo "Building ${BUILD_TARGET} (${JOBS} jobs) ..."
cmake --build "${BUILD_DIR}" --target "${BUILD_TARGET}" --parallel "${JOBS}"

NRO_PATH="${BUILD_DIR}/${BUILD_TARGET}"
if [[ ! -f "${NRO_PATH}" ]]; then
    echo "ERROR: build completed without producing ${NRO_PATH}" >&2
    exit 1
fi

echo "NRO: ${NRO_PATH}"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${NRO_PATH}"
fi
