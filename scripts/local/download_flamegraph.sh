#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

remote_host_override="${RRS_REMOTE_HOST:-}"
remote_repo_override="${RRS_REMOTE_REPO:-}"
if [[ -f "${repo_root}/.env" ]]; then
    set -a
    source "${repo_root}/.env"
    set +a
fi

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <svg-file-name|latest>" >&2
    echo "configuration: repo-root .env or RRS_REMOTE_HOST/RRS_REMOTE_REPO environment variables" >&2
    echo "example: $0 20260713_4c4g_conn1024_io3_worker1.svg" >&2
    echo "example: $0 latest" >&2
    exit 1
fi

remote_host="${remote_host_override:-${RRS_REMOTE_HOST:-}}"
remote_repo="${remote_repo_override:-${RRS_REMOTE_REPO:-/root/rrs}}"
svg_name="$1"

if [[ -z "${remote_host}" ]]; then
    echo "RRS_REMOTE_HOST is required in ${repo_root}/.env or the process environment" >&2
    exit 1
fi

mkdir -p "${repo_root}/perf/flamegraphs"

if [[ "${svg_name}" == "latest" ]]; then
    svg_path="$(ssh "${remote_host}" "find '${remote_repo}/perf/flamegraphs' -maxdepth 1 -type f -name '*.svg' -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-")"
    if [[ -z "${svg_path}" ]]; then
        echo "no remote flamegraph found under ${remote_repo}/perf/flamegraphs" >&2
        exit 1
    fi
    svg_name="$(basename "${svg_path}")"
else
    svg_path="${remote_repo}/perf/flamegraphs/${svg_name}"
fi

scp "${remote_host}:${svg_path}" "${repo_root}/perf/flamegraphs/${svg_name}"
echo "downloaded ${repo_root}/perf/flamegraphs/${svg_name}"
