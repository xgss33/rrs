#!/usr/bin/env bash
set -euo pipefail

mode="${1:-release}"
clean="${2:-}"

if [[ "${mode}" != "release" && "${mode}" != "perf" ]]; then
    echo "usage: $0 [release|perf] [clean]" >&2
    exit 1
fi

if [[ -n "${clean}" && "${clean}" != "clean" ]]; then
    echo "usage: $0 [release|perf] [clean]" >&2
    exit 1
fi

if [[ "${clean}" == "clean" ]]; then
    rm -rf build
fi

: "${CC:=gcc-13}"
: "${CXX:=g++-13}"
export CC
export CXX

cmake_args=(
    -S .
    -B build
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
cmake --build build -j"$(nproc)"
