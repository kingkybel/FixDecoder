#!/usr/bin/env bash
set -euo pipefail

PROFILE_ROOT="${PROFILE_ROOT:-/profiles}"
PROFILE_UI_HOST="${PROFILE_UI_HOST:-0.0.0.0}"
PROFILE_UI_PORT="${PROFILE_UI_PORT:-8080}"
AUTO_FLAMEGRAPH="${AUTO_FLAMEGRAPH:-1}"
FORCE_FLAMEGRAPH_REBUILD="${FORCE_FLAMEGRAPH_REBUILD:-0}"

readonly STACKCOLLAPSE="/opt/FlameGraph/stackcollapse-perf.pl"
readonly FLAMEGRAPH="/opt/FlameGraph/flamegraph.pl"

usage() {
    cat <<'USAGE'
Usage:
  profiler-ui                 Serve /profiles over HTTP (default)
  profiler-ui serve
  profiler-ui record <pid> <seconds> [freq] [output]
  profiler-ui flamegraph <perf.data> [output.svg]

Environment:
  PROFILE_ROOT=/profiles
  PROFILE_UI_HOST=0.0.0.0
  PROFILE_UI_PORT=8080
  AUTO_FLAMEGRAPH=1
  FORCE_FLAMEGRAPH_REBUILD=0
USAGE
}

record_samples() {
    local target_pid="$1"
    local seconds="$2"
    local freq="$3"
    local output="$4"
    local debug_file

    debug_file="$(dirname "${output}")/perf_record_debug.txt"
    : > "${debug_file}"
    {
        echo "target_pid=${target_pid}"
        echo "seconds=${seconds}"
        echo "freq=${freq}"
        echo "output=${output}"
    } >> "${debug_file}"

    # Prefer user-space call stacks, but fall back if kernel/capabilities block them.
    if perf record -F "${freq}" -e cpu-clock:u --call-graph dwarf,16384 -p "${target_pid}" -o "${output}" -- sleep "${seconds}" >> "${debug_file}" 2>&1; then
        echo "mode=cpu-clock:u+dwarf" >> "${debug_file}"
        return 0
    fi
    if perf record -F "${freq}" -e cpu-clock:u --call-graph fp -p "${target_pid}" -o "${output}" -- sleep "${seconds}" >> "${debug_file}" 2>&1; then
        echo "mode=cpu-clock:u+fp" >> "${debug_file}"
        return 0
    fi
    if perf record -F "${freq}" -e cpu-clock --call-graph fp -p "${target_pid}" -o "${output}" -- sleep "${seconds}" >> "${debug_file}" 2>&1; then
        echo "mode=cpu-clock+fp" >> "${debug_file}"
        return 0
    fi
    if perf record -e task-clock -p "${target_pid}" -o "${output}" -- sleep "${seconds}" >> "${debug_file}" 2>&1; then
        echo "mode=task-clock(no-callgraph)" >> "${debug_file}"
        return 0
    fi

    echo "mode=failed" >> "${debug_file}"
    return 2
}

render_flamegraph() {
    local perf_data="$1"
    local output_svg="$2"
    local perf_dir
    local perf_out
    local perf_folded
    local perf_script_log
    local flame_error

    if [[ ! -f "${perf_data}" ]]; then
        echo "perf.data not found: ${perf_data}" >&2
        return 2
    fi

    if [[ -f "${output_svg}" && "${FORCE_FLAMEGRAPH_REBUILD}" != "1" ]]; then
        echo "Flamegraph already exists, skipping: ${output_svg}"
        return 0
    fi

    perf_dir="$(dirname "${perf_data}")"
    perf_out="${perf_dir}/perf.out"
    perf_folded="${perf_dir}/perf.folded"
    perf_script_log="${perf_dir}/perf_script.log"
    flame_error="${perf_dir}/flamegraph_error.txt"

    echo "Generating flamegraph:"
    echo "  input:  ${perf_data}"
    echo "  output: ${output_svg}"
    perf script -i "${perf_data}" > "${perf_out}" 2> "${perf_script_log}" || true
    if [[ ! -s "${perf_out}" ]]; then
        cat > "${flame_error}" <<EOF
Flamegraph generation failed: perf script produced no stack data.
Input: ${perf_data}
Likely causes:
- sampling permissions blocked by kernel perf_event settings
- target process exited before/during recording
- no user-space samples captured for selected event
See: ${perf_script_log}
See: ${perf_dir}/perf_record_debug.txt
EOF
        echo "No perf script output. See ${flame_error}" >&2
        return 2
    fi

    "${STACKCOLLAPSE}" --all "${perf_out}" > "${perf_folded}"
    if [[ ! -s "${perf_folded}" ]]; then
        cat > "${flame_error}" <<EOF
Flamegraph generation failed: collapsed stacks are empty.
Input: ${perf_data}
perf output: ${perf_out}
perf log: ${perf_script_log}
record log: ${perf_dir}/perf_record_debug.txt
EOF
        echo "No collapsed stacks. See ${flame_error}" >&2
        return 2
    fi

    "${FLAMEGRAPH}" "${perf_folded}" > "${output_svg}"
}

render_all_flamegraphs() {
    local perf_data
    local output_svg
    while IFS= read -r -d '' perf_data; do
        output_svg="$(dirname "${perf_data}")/flame.svg"
        render_flamegraph "${perf_data}" "${output_svg}" || true
    done < <(find "${PROFILE_ROOT}" -type f -name "perf.data" -print0)
}

write_root_index() {
    local latest
    latest="$(find "${PROFILE_ROOT}" -maxdepth 1 -mindepth 1 -type d -name "hotpaths_*" | sort | tail -n 1 || true)"
    if [[ -n "${latest}" && -f "${latest}/index.html" ]]; then
        ln -sfn "$(basename "${latest}")" "${PROFILE_ROOT}/latest"
        cat > "${PROFILE_ROOT}/index.html" <<'EOF'
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta http-equiv="refresh" content="0; url=./latest/index.html">
  <title>FIX Coverage Dashboard</title>
</head>
<body>
  <p>Redirecting to latest dashboard: <a href="./latest/index.html">open</a></p>
</body>
</html>
EOF
        return 0
    fi

    cat > "${PROFILE_ROOT}/index.html" <<'EOF'
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>FIX Coverage Dashboard</title>
</head>
<body>
  <h1>No dashboard yet</h1>
  <p>Run profile_hotpaths.sh first to generate coverage artifacts.</p>
</body>
</html>
EOF
}

serve() {
    mkdir -p "${PROFILE_ROOT}"
    if [[ "${AUTO_FLAMEGRAPH}" == "1" ]]; then
        render_all_flamegraphs
    fi
    write_root_index

    echo "Profiler UI serving ${PROFILE_ROOT}"
    echo "Open: http://localhost:${PROFILE_UI_PORT}/"
    exec python3 -m http.server "${PROFILE_UI_PORT}" --bind "${PROFILE_UI_HOST}" --directory "${PROFILE_ROOT}"
}

main() {
    local cmd="${1:-serve}"

    case "${cmd}" in
        serve)
            serve
            ;;
        flamegraph)
            shift || true
            if [[ $# -lt 1 || $# -gt 2 ]]; then
                usage
                exit 2
            fi

            local input="$1"
            local output=""
            if [[ "${input}" != /* ]]; then
                input="${PROFILE_ROOT}/${input}"
            fi
            if [[ $# -eq 2 ]]; then
                output="$2"
                if [[ "${output}" != /* ]]; then
                    output="${PROFILE_ROOT}/${output}"
                fi
            else
                output="$(dirname "${input}")/flame.svg"
            fi
            mkdir -p "$(dirname "${output}")"
            render_flamegraph "${input}" "${output}"
            ;;
        record)
            shift || true
            if [[ $# -lt 2 || $# -gt 4 ]]; then
                usage
                exit 2
            fi
            local target_pid="$1"
            local seconds="$2"
            local freq="${3:-99}"
            local output="${4:-/profiles/perf.data}"
            if [[ "${output}" != /* ]]; then
                output="${PROFILE_ROOT}/${output}"
            fi
            mkdir -p "$(dirname "${output}")"
            record_samples "${target_pid}" "${seconds}" "${freq}" "${output}"
            ;;
        -h|--help|help)
            usage
            ;;
        *)
            echo "Unknown command: ${cmd}" >&2
            usage
            exit 2
            ;;
    esac
}

main "$@"
