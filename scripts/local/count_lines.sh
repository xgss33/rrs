#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

find "${repo_root}" \
    \( -path "${repo_root}/src/*" -o \
       -path "${repo_root}/scripts/*" -o \
       -path "${repo_root}/CMakeLists.txt" -o \
       -path "${repo_root}/README.md" \) \
    -type f \
    \( -name '*.h' -o \
       -name '*.cpp' -o \
       -name '*.sh' -o \
       -name 'CMakeLists.txt' -o \
       -name 'README.md' \) \
    | sort \
    | xargs wc -l
