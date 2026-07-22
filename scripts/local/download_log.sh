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
    echo "usage: $0 <log-file-name|latest>" >&2
    echo "configuration: repo-root .env or RRS_REMOTE_HOST/RRS_REMOTE_REPO environment variables" >&2
    echo "example: $0 20260713_105003_io3_worker1.log" >&2
    echo "example: $0 latest" >&2
    exit 1
fi

remote_host="${remote_host_override:-${RRS_REMOTE_HOST:-}}"
remote_repo="${remote_repo_override:-${RRS_REMOTE_REPO:-/root/rrs}}"
log_name="$1"

if [[ -z "${remote_host}" ]]; then
    echo "RRS_REMOTE_HOST is required in ${repo_root}/.env or the process environment" >&2
    exit 1
fi

mkdir -p "${repo_root}/logs"

if [[ "${log_name}" == "latest" ]]; then
    log_path="$(ssh "${remote_host}" "find '${remote_repo}/logs' -maxdepth 1 -type f -name '*.log' -printf '%T@ %p\n' | sort -nr | head -n 1 | cut -d' ' -f2-")"
    if [[ -z "${log_path}" ]]; then
        echo "no remote log file found under ${remote_repo}/logs" >&2
        exit 1
    fi
    log_name="$(basename "${log_path}")"
else
    log_path="${remote_repo}/logs/${log_name}"
fi

scp "${remote_host}:${log_path}" "${repo_root}/logs/${log_name}"
echo "downloaded ${repo_root}/logs/${log_name}"
