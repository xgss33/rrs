#!/usr/bin/env bash
set -euo pipefail

mode="${1:-release}"
clean="${2:-}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

if [[ "${mode}" != "release" && "${mode}" != "perf" ]]; then
    echo "usage: $0 [release|perf] [clean]" >&2
    exit 1
fi

if [[ -n "${clean}" && "${clean}" != "clean" ]]; then
    echo "usage: $0 [release|perf] [clean]" >&2
    exit 1
fi

if [[ "${clean}" == "clean" ]]; then
    rm -rf "${repo_root}/build"
fi

: "${CC:=gcc-13}"
: "${CXX:=g++-13}"
export CC
export CXX

cmake_args=(
    -S "${repo_root}"
    -B "${repo_root}/build"
    -G Ninja
)

if [[ "${mode}" == "release" ]]; then
    cmake_args+=(-DCMAKE_BUILD_TYPE=Release)
else
    cmake_args+=(
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
        "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -g -fno-omit-frame-pointer"
    )
fi

cmake "${cmake_args[@]}"
cmake --build "${repo_root}/build" -j"$(nproc)"
