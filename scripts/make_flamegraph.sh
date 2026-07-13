#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
    echo "usage: $0 <output-name-without-svg> [pid] [seconds]" >&2
    exit 1
fi

output_name="$1"
seconds="${3:-60}"
if [[ $# -ge 2 ]]; then
    pid="$2"
else
    pid="$(pidof rrs || true)"
fi

if [[ -z "${pid}" ]]; then
    echo "rrs process not found; pass pid explicitly" >&2
    exit 1
fi

flamegraph_dir="${FLAMEGRAPH_DIR:-/root/FlameGraph}"
if [[ ! -x "${flamegraph_dir}/stackcollapse-perf.pl" || ! -x "${flamegraph_dir}/flamegraph.pl" ]]; then
    echo "FlameGraph scripts not found under ${flamegraph_dir}" >&2
    exit 1
fi

mkdir -p perf/flamegraphs

perf_data="perf_${output_name}.data"
perf_script="out_${output_name}.perf"
folded="out_${output_name}.folded"
svg="perf/flamegraphs/${output_name}.svg"

perf record -F 99 -p "${pid}" -g -o "${perf_data}" -- sleep "${seconds}"
perf script -i "${perf_data}" > "${perf_script}"
"${flamegraph_dir}/stackcollapse-perf.pl" "${perf_script}" > "${folded}"
"${flamegraph_dir}/flamegraph.pl" "${folded}" \
    --width 1800 \
    --fontsize 12 \
    --title "${output_name}" \
    > "${svg}"

echo "${svg}"
