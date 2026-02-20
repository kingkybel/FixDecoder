#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE_FILE="${REPO_ROOT}/test/integration/compose.yml"
COMPOSE_PROJECT="fix-controller-test"
BIN_VOLUME="${COMPOSE_PROJECT}_fix-controller-bin"
CONTAINER_SCRIPTS_DIR="/workspace/test/integration/container-scripts"

BASE_IMAGE="${FIX_BASE_IMAGE:-ubuntu:24.04}"
COMPILER_FAMILY="${FIX_COMPILER_FAMILY:-clang}"
COMPILER_VERSION="${FIX_COMPILER_VERSION:-}"
PAYLOAD_SIZE="${FIX_PERF_PAYLOAD_SIZE:-2048}"
DURATION_SECONDS=30
MESSAGES_PER_SECOND="${FIX_MESSAGES_PER_SECOND:-200}"
HOST_UID="${FIX_HOST_UID:-$(id -u)}"
HOST_GID="${FIX_HOST_GID:-$(id -g)}"
PROFILE_UI_PORT="${FIX_PROFILE_UI_PORT:-8080}"
COLLECT_PERF="${FIX_COLLECT_PERF:-1}"
PERF_SAMPLE_HZ="${FIX_PERF_SAMPLE_HZ:-99}"
PROFILER_IMAGE="${FIX_PROFILER_IMAGE:-fix-controller-profiler-ui:alpine}"
ALL_FIX_VERSIONS=(
    "FIX.4.0"
    "FIX.4.1"
    "FIX.4.2"
    "FIX.4.3"
    "FIX.4.4"
    "FIXT.1.1"
)
FIX_VERSIONS=("${FIX_BEGIN_STRING:-FIX.4.4}")
PROFILE_BASE_DIR="${REPO_ROOT}/test/integration/profiles"

ensure_profiles_dir_writable() {
    mkdir -p "${PROFILE_BASE_DIR}" 2>/dev/null || true
    if [[ -w "${PROFILE_BASE_DIR}" ]]; then
        return 0
    fi

    printf '[%s] profiles dir is not writable, attempting ownership fix via docker\n' "$(date +'%Y-%m-%d %H:%M:%S')" >&2
    if docker run --rm \
        -v "${REPO_ROOT}/test/integration:/hostfix" \
        alpine:3.20 \
        sh -lc "mkdir -p /hostfix/profiles && chown -R ${HOST_UID}:${HOST_GID} /hostfix/profiles" \
        >/dev/null 2>&1; then
        if [[ -w "${PROFILE_BASE_DIR}" ]]; then
            return 0
        fi
    fi

    echo "Unable to make '${PROFILE_BASE_DIR}' writable." >&2
    echo "Run: sudo chown -R ${HOST_UID}:${HOST_GID} '${PROFILE_BASE_DIR}'" >&2
    exit 2
}

parse_duration_seconds() {
    local raw="$1"
    local trimmed
    local normalized
    local number
    local unit
    local factor
    local seconds

    trimmed="$(printf '%s' "${raw}" | tr -d '[:space:]')"
    normalized="${trimmed,,}"
    if [[ ! "${normalized}" =~ ^([0-9]*\.?[0-9]+)([smh]?)$ ]]; then
        echo ""
        return 1
    fi

    number="${BASH_REMATCH[1]}"
    unit="${BASH_REMATCH[2]}"
    case "${unit}" in
        ""|s) factor=1 ;;
        m) factor=60 ;;
        h) factor=3600 ;;
        *) echo ""; return 1 ;;
    esac

    seconds="$(awk -v n="${number}" -v f="${factor}" 'BEGIN { x=n*f; if (x < 1) x = 1; printf "%.0f", x }')"
    if [[ -z "${seconds}" || ! "${seconds}" =~ ^[0-9]+$ ]]; then
        echo ""
        return 1
    fi
    echo "${seconds}"
}

usage() {
    cat <<'USAGE'
Usage: profile_hotpaths.sh [options]

Build an instrumented docker binary, run exchange/client performance traffic,
and generate hotspot + line-heatmap reports.

Options:
  -d, --duration <value>         Conversation duration (default: 30 seconds).
                                 Supports s/m/h suffix and decimals.
                                 Examples: 10s, 1m, 0.5h, 45
  -f, --fix-versions <version...>
                                 Filter FIX versions to profile.
                                 Accepts full version strings or shorthand.
                                 Examples: -f FIX.4.4, -f 11 Fix-40
  -o, --os <image>               Base image for builder/runtime (default: ubuntu:24.04)
  -c, --compiler <name>          Compiler family (default: clang)
  -v, --compiler-version <num>   Compiler major version (default: latest available)
  -p, --payload-size <bytes>     FIX_PERF_PAYLOAD_SIZE (default: 2048)
      --no-perf                  Disable automatic perf.data + flame.svg collection
      --perf-hz <value>          perf sample frequency Hz (default: 99)
  -h, --help                     Show this help
USAGE
}

contains_version() {
    local candidate="$1"
    shift
    local item
    for item in "$@"; do
        if [[ "${item}" == "${candidate}" ]]; then
            return 0
        fi
    done
    return 1
}

normalize_fix_version() {
    local raw="$1"
    local compact
    compact="$(printf '%s' "${raw}" | tr '[:lower:]' '[:upper:]' | tr -cd 'A-Z0-9')"

    case "${compact}" in
        40|FIX40|FIX400|FIX4)
            echo "FIX.4.0"
            ;;
        41|FIX41|FIX410)
            echo "FIX.4.1"
            ;;
        42|FIX42|FIX420)
            echo "FIX.4.2"
            ;;
        43|FIX43|FIX430)
            echo "FIX.4.3"
            ;;
        44|FIX44|FIX440)
            echo "FIX.4.4"
            ;;
        11|FIXT11|FIXT1|FIXT110)
            echo "FIXT.1.1"
            ;;
        *)
            echo ""
            ;;
    esac
}

normalize_compiler_family() {
    local raw="$1"
    local lower
    lower="$(printf '%s' "${raw}" | tr '[:upper:]' '[:lower:]')"
    case "${lower}" in
        gcc|g++)
            echo "g++"
            ;;
        clang|llvm)
            echo "clang"
            ;;
        *)
            echo ""
            ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--duration)
            shift
            [[ $# -gt 0 ]] || { echo "Missing value for --duration" >&2; exit 2; }
            parsed_duration="$(parse_duration_seconds "$1")" || true
            [[ -n "${parsed_duration:-}" ]] || {
                echo "Invalid duration '$1'. Use values like 10s, 1m, 0.5h, or 45." >&2
                exit 2
            }
            DURATION_SECONDS="${parsed_duration}"
            shift
            ;;
        -f|--fix-versions)
            shift
            [[ $# -gt 0 && "$1" != -* ]] || { echo "Missing value for --fix-versions/-f" >&2; exit 2; }
            FIX_VERSIONS=()
            while [[ $# -gt 0 && "$1" != -* ]]; do
                normalized_version="$(normalize_fix_version "$1")"
                [[ -n "${normalized_version}" ]] || {
                    echo "Unsupported FIX version token '$1'" >&2
                    exit 2
                }
                if ! contains_version "${normalized_version}" "${FIX_VERSIONS[@]}"; then
                    FIX_VERSIONS+=("${normalized_version}")
                fi
                shift
            done
            ;;
        -o|--os|--base-image|-b)
            shift
            [[ $# -gt 0 ]] || { echo "Missing value for --base-image" >&2; exit 2; }
            BASE_IMAGE="$1"
            shift
            ;;
        -c|--compiler)
            shift
            [[ $# -gt 0 ]] || { echo "Missing value for --compiler" >&2; exit 2; }
            normalized_compiler="$(normalize_compiler_family "$1")"
            [[ -n "${normalized_compiler}" ]] || {
                echo "Unsupported compiler family '$1' (use g++|gcc|clang)" >&2
                exit 2
            }
            COMPILER_FAMILY="${normalized_compiler}"
            shift
            ;;
        -v|--compiler-version)
            shift
            [[ $# -gt 0 ]] || { echo "Missing value for --compiler-version" >&2; exit 2; }
            COMPILER_VERSION="$1"
            shift
            ;;
        -p|--payload-size)
            shift
            [[ $# -gt 0 ]] || { echo "Missing value for --payload-size" >&2; exit 2; }
            [[ "$1" =~ ^[0-9]+$ ]] || { echo "Payload size must be an integer" >&2; exit 2; }
            PAYLOAD_SIZE="$1"
            shift
            ;;
        --no-perf)
            COLLECT_PERF=0
            shift
            ;;
        --perf-hz)
            shift
            [[ $# -gt 0 ]] || { echo "Missing value for --perf-hz" >&2; exit 2; }
            [[ "$1" =~ ^[0-9]+$ ]] || { echo "perf sample frequency must be an integer" >&2; exit 2; }
            PERF_SAMPLE_HZ="$1"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ ${#FIX_VERSIONS[@]} -eq 0 ]]; then
    FIX_VERSIONS=("${ALL_FIX_VERSIONS[@]}")
fi

if [[ "${COMPILER_FAMILY,,}" == "gcc" || "${COMPILER_FAMILY,,}" == "g++" ]]; then
    echo "Coverage heatmaps require clang/llvm-profdata/llvm-cov. Use --compiler clang." >&2
    exit 2
fi

timestamp="$(date +%Y%m%d_%H%M%S)"
ensure_profiles_dir_writable
PROFILE_ROOT="${PROFILE_BASE_DIR}/hotpaths_${timestamp}"
mkdir -p "${PROFILE_ROOT}"
UNIT_PROFILE_DIR="${PROFILE_ROOT}/unit_tests"
UNIT_RAW_DIR="${UNIT_PROFILE_DIR}/raw"
mkdir -p "${UNIT_RAW_DIR}"

sanitize_for_tag() {
    local value="${1,,}"
    value="${value//g++/gxx}"
    value="${value//clang++/clangxx}"
    value="${value//[\/:@ ]/-}"
    printf '%s' "${value}" | sed -E 's/[^a-z0-9_.-]+/-/g; s/-+/-/g; s/^-+//; s/-+$//'
}

compiler_tag="$(sanitize_for_tag "${COMPILER_FAMILY}")"
compiler_ver_tag="$(sanitize_for_tag "${COMPILER_VERSION}")"
[[ -n "${compiler_ver_tag}" ]] || compiler_ver_tag="latest"
image_suffix="$(sanitize_for_tag "${BASE_IMAGE}")-${compiler_tag}-${compiler_ver_tag}-hotpath"
BUILDER_IMAGE="fix-controller-builder:${image_suffix}"
RUNTIME_IMAGE="fix-controller-runtime:${image_suffix}"

conversation_messages=$(( DURATION_SECONDS * MESSAGES_PER_SECOND ))
if [[ ${conversation_messages} -lt 200 ]]; then
    conversation_messages=200
fi

cleanup() {
    docker compose -f "${COMPOSE_FILE}" down --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT

printf '[%s] Output folder: %s\n' "$(date +'%Y-%m-%d %H:%M:%S')" "${PROFILE_ROOT}"
printf '[%s] Building docker images (%s, %s)\n' "$(date +'%Y-%m-%d %H:%M:%S')" "${BUILDER_IMAGE}" "${RUNTIME_IMAGE}"
printf '[%s] FIX versions under profile: %s\n' "$(date +'%Y-%m-%d %H:%M:%S')" "${FIX_VERSIONS[*]}"

env \
    FIX_BASE_IMAGE="${BASE_IMAGE}" \
    FIX_COMPILER_FAMILY="${COMPILER_FAMILY}" \
    FIX_COMPILER_VERSION="${COMPILER_VERSION}" \
    FIX_BUILDER_IMAGE="${BUILDER_IMAGE}" \
    FIX_RUNTIME_IMAGE="${RUNTIME_IMAGE}" \
    docker compose -f "${COMPOSE_FILE}" build fix-builder fix-exchange-1

if [[ "${COLLECT_PERF}" == "1" ]]; then
    if [[ -r /proc/sys/kernel/perf_event_paranoid ]]; then
        host_perf_paranoid="$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "")"
        if [[ "${host_perf_paranoid}" =~ ^[0-9]+$ ]] && (( host_perf_paranoid > 2 )); then
            printf '[%s] Warning: host kernel.perf_event_paranoid=%s may block user-space sampling.\n' \
                "$(date +'%Y-%m-%d %H:%M:%S')" "${host_perf_paranoid}" >&2
            printf '[%s]          Consider: sudo sysctl -w kernel.perf_event_paranoid=1\n' \
                "$(date +'%Y-%m-%d %H:%M:%S')" >&2
        fi
    fi

    printf '[%s] Building profiler UI image (perf + FlameGraph)\n' "$(date +'%Y-%m-%d %H:%M:%S')"
    if ! docker compose -f "${COMPOSE_FILE}" --profile profiler-ui build fix-profiler-ui >/dev/null; then
        printf '[%s] Warning: profiler UI image build failed; continuing without perf sampling\n' "$(date +'%Y-%m-%d %H:%M:%S')" >&2
        COLLECT_PERF=0
    fi
fi

docker volume create "${BIN_VOLUME}" >/dev/null

printf '[%s] Building instrumented binary\n' "$(date +'%Y-%m-%d %H:%M:%S')"
docker run --rm \
    -e FIX_COMPILER_VERSION="${COMPILER_VERSION}" \
    -v "${REPO_ROOT}:/workspace" \
    -v "${BIN_VOLUME}:/out" \
    "${BUILDER_IMAGE}" \
    "${CONTAINER_SCRIPTS_DIR}/build_instrumented_targets.sh"

printf '[%s] Running unit tests with coverage instrumentation\n' "$(date +'%Y-%m-%d %H:%M:%S')"
docker run --rm \
    -v "${UNIT_PROFILE_DIR}:/profiles" \
    -v "${BIN_VOLUME}:/out:ro" \
    -v "${REPO_ROOT}:/workspace:ro" \
    "${BUILDER_IMAGE}" \
    "${CONTAINER_SCRIPTS_DIR}/run_unit_tests_profiled.sh"

printf '[%s] Generating unit-test coverage reports\n' "$(date +'%Y-%m-%d %H:%M:%S')"
docker run --rm \
    -v "${UNIT_PROFILE_DIR}:/profiles" \
    -v "${BIN_VOLUME}:/out:ro" \
    -v "${REPO_ROOT}:/workspace:ro" \
    "${BUILDER_IMAGE}" \
    "${CONTAINER_SCRIPTS_DIR}/generate_unit_coverage.sh"

docker run --rm \
    -e HOST_UID="${HOST_UID}" \
    -e HOST_GID="${HOST_GID}" \
    -v "${UNIT_PROFILE_DIR}:/profiles" \
    -v "${REPO_ROOT}:/workspace:ro" \
    "${BUILDER_IMAGE}" \
    "${CONTAINER_SCRIPTS_DIR}/fix_ownership.sh" /profiles

printf '  [unit-tests] raw profiles:   %s\n' "${UNIT_RAW_DIR}"
printf '  [unit-tests] merged profile: %s\n' "${UNIT_PROFILE_DIR}/merged.profdata"
printf '  [unit-tests] coverage:       %s\n' "${UNIT_PROFILE_DIR}/unit_coverage_report.txt"
printf '  [unit-tests] heatmap (text): %s\n' "${UNIT_PROFILE_DIR}/unit_heatmap.txt"
printf '  [unit-tests] heatmap (html): %s\n' "${UNIT_PROFILE_DIR}/heatmap_html/index.html"

for begin_string in "${FIX_VERSIONS[@]}"; do
    version_token="$(printf '%s' "${begin_string}" | tr '.:' '__' | tr -cd 'A-Za-z0-9_')"
    PROFILE_DIR="${PROFILE_ROOT}/${version_token}"
    RAW_DIR="${PROFILE_DIR}/raw"
    mkdir -p "${RAW_DIR}"
    perf_wait_pid=""
    perf_data_file="${PROFILE_DIR}/perf.data"
    flame_svg_file="${PROFILE_DIR}/flame.svg"

    printf '[%s] Running instrumented conversation for %ss (%s)\n' "$(date +'%Y-%m-%d %H:%M:%S')" "${DURATION_SECONDS}" "${begin_string}"

    env \
        FIX_BASE_IMAGE="${BASE_IMAGE}" \
        FIX_COMPILER_FAMILY="${COMPILER_FAMILY}" \
        FIX_COMPILER_VERSION="${COMPILER_VERSION}" \
        FIX_BUILDER_IMAGE="${BUILDER_IMAGE}" \
        FIX_RUNTIME_IMAGE="${RUNTIME_IMAGE}" \
        FIX_PROFILE_OUTPUT_HOST_DIR="${RAW_DIR}" \
        LLVM_PROFILE_FILE="/profiles/%h-%p.profraw" \
        FIX_SCENARIO=performance \
        FIX_RUNTIME_SECONDS="${DURATION_SECONDS}" \
        FIX_CONVERSATION_MESSAGES="${conversation_messages}" \
        FIX_PERF_PAYLOAD_SIZE="${PAYLOAD_SIZE}" \
        FIX_LOOP_PAYLOADS_UNTIL_RUNTIME=1 \
        FIX_MAX_IN_FLIGHT="${FIX_MAX_IN_FLIGHT:-128}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${begin_string}" \
        FIX_CLIENT_1_BEGIN_STRING="${begin_string}" \
        FIX_REALISTIC_MESSAGES_DIR="/workspace/data/samples/realistic" \
        docker compose -f "${COMPOSE_FILE}" --profile single-client up -d

    if [[ "${COLLECT_PERF}" == "1" ]]; then
        client_cid="$(docker compose -f "${COMPOSE_FILE}" ps -q fix-client-1 || true)"
        if [[ -n "${client_cid}" ]]; then
            client_pid="$(docker inspect -f '{{.State.Pid}}' "${client_cid}" 2>/dev/null || true)"
            if [[ "${client_pid}" =~ ^[0-9]+$ ]] && [[ "${client_pid}" -gt 0 ]]; then
                printf '[%s] Collecting perf samples (%s, %s Hz)\n' "$(date +'%Y-%m-%d %H:%M:%S')" "${begin_string}" "${PERF_SAMPLE_HZ}"
                docker run --rm --privileged --pid=host \
                    -v "${PROFILE_DIR}:/profiles" \
                    "${PROFILER_IMAGE}" \
                    record "${client_pid}" "${DURATION_SECONDS}" "${PERF_SAMPLE_HZ}" "/profiles/perf.data" \
                    >/dev/null 2>&1 &
                perf_wait_pid="$!"
            else
                printf '[%s] Warning: unable to resolve fix-client-1 PID, skipping perf collection (%s)\n' "$(date +'%Y-%m-%d %H:%M:%S')" "${begin_string}" >&2
            fi
        else
            printf '[%s] Warning: fix-client-1 container not found, skipping perf collection (%s)\n' "$(date +'%Y-%m-%d %H:%M:%S')" "${begin_string}" >&2
        fi
    fi

    sleep "${DURATION_SECONDS}"
    if [[ -n "${perf_wait_pid}" ]]; then
        wait "${perf_wait_pid}" || true
    fi
    docker compose -f "${COMPOSE_FILE}" stop --timeout 5 fix-client-1 fix-exchange-1 >/dev/null 2>&1 || true
    docker compose -f "${COMPOSE_FILE}" down --remove-orphans >/dev/null 2>&1 || true

    printf '[%s] Generating hotspot and heatmap reports (%s)\n' "$(date +'%Y-%m-%d %H:%M:%S')" "${begin_string}"
    docker run --rm \
        -e HOST_UID="${HOST_UID}" \
        -e HOST_GID="${HOST_GID}" \
        -v "${PROFILE_DIR}:/profiles" \
        -v "${BIN_VOLUME}:/out:ro" \
        -v "${REPO_ROOT}:/workspace:ro" \
        "${BUILDER_IMAGE}" \
        "${CONTAINER_SCRIPTS_DIR}/generate_integration_coverage.sh"

    if [[ -f "${perf_data_file}" ]]; then
        docker run --rm \
            -v "${PROFILE_DIR}:/profiles" \
            "${PROFILER_IMAGE}" \
            flamegraph /profiles/perf.data /profiles/flame.svg >/dev/null 2>&1 || true

        if [[ ! -f "${flame_svg_file}" && -f "${PROFILE_DIR}/flamegraph_error.txt" ]]; then
            cat > "${flame_svg_file}" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="220" viewBox="0 0 1200 220">
  <rect width="1200" height="220" fill="#f8fafc"/>
  <text x="24" y="50" font-family="sans-serif" font-size="28" fill="#b91c1c">Flamegraph not available for this run</text>
  <text x="24" y="95" font-family="sans-serif" font-size="18" fill="#0f172a">Reason: no usable perf stack samples were captured.</text>
  <text x="24" y="130" font-family="sans-serif" font-size="16" fill="#334155">See file: ${PROFILE_DIR}/flamegraph_error.txt</text>
  <text x="24" y="160" font-family="sans-serif" font-size="16" fill="#334155">See file: ${PROFILE_DIR}/perf_script.log</text>
</svg>
EOF
        fi
    fi

    docker run --rm \
        -e HOST_UID="${HOST_UID}" \
        -e HOST_GID="${HOST_GID}" \
        -v "${PROFILE_DIR}:/profiles" \
        -v "${REPO_ROOT}:/workspace:ro" \
        "${BUILDER_IMAGE}" \
        "${CONTAINER_SCRIPTS_DIR}/fix_ownership.sh" /profiles

    printf '  [%s] raw profiles:   %s\n' "${begin_string}" "${RAW_DIR}"
    printf '  [%s] merged profile: %s\n' "${begin_string}" "${PROFILE_DIR}/merged.profdata"
    printf '  [%s] hotpaths:       %s\n' "${begin_string}" "${PROFILE_DIR}/hotpaths_report.txt"
    printf '  [%s] heatmap (text): %s\n' "${begin_string}" "${PROFILE_DIR}/heatmap.txt"
    printf '  [%s] heatmap (html): %s\n' "${begin_string}" "${PROFILE_DIR}/heatmap_html/index.html"
    if [[ -f "${perf_data_file}" ]]; then
        printf '  [%s] perf data:      %s\n' "${begin_string}" "${perf_data_file}"
    fi
    if [[ -f "${flame_svg_file}" ]]; then
        printf '  [%s] flamegraph:     %s\n' "${begin_string}" "${flame_svg_file}"
    fi
done

first_version_token="$(printf '%s' "${FIX_VERSIONS[0]}" | tr '.:' '__' | tr -cd 'A-Za-z0-9_')"
cat > "${PROFILE_ROOT}/index.html" <<EOF
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>FIX Coverage Dashboard (${timestamp})</title>
  <style>
    :root { --bg:#f4f4ef; --fg:#1f2937; --accent:#0f766e; --muted:#64748b; --card:#ffffff; }
    body { margin:0; font-family: "IBM Plex Sans", "Segoe UI", sans-serif; background:var(--bg); color:var(--fg); }
    header { padding:16px 20px; background:linear-gradient(90deg,#d1fae5,#ccfbf1); border-bottom:1px solid #cbd5e1; }
    h1 { margin:0; font-size:20px; }
    p { margin:6px 0 0; color:var(--muted); }
    .tabs { display:flex; gap:8px; padding:12px 20px; }
    .tab { border:1px solid #94a3b8; background:#fff; border-radius:10px; padding:8px 12px; cursor:pointer; font-weight:600; }
    .tab.active { background:var(--accent); color:#fff; border-color:var(--accent); }
    .panel { display:none; padding:0 20px 20px; }
    .panel.active { display:block; }
    .links { background:var(--card); border:1px solid #d1d5db; border-radius:12px; padding:10px 12px; margin-bottom:12px; }
    .links a { color:#0f766e; margin-right:12px; text-decoration:none; font-weight:600; }
    iframe { width:100%; height:78vh; border:1px solid #d1d5db; border-radius:12px; background:#fff; }
  </style>
</head>
<body>
  <header>
    <h1>FIX Coverage Dashboard</h1>
    <p>Run: hotpaths_${timestamp}</p>
  </header>
  <div class="tabs">
    <button class="tab active" data-target="unit">Unit-Test Coverage</button>
    <button class="tab" data-target="integration">Integration Coverage + Heatmap</button>
  </div>
  <section id="unit" class="panel active">
    <div class="links">
      <a href="unit_tests/heatmap_html/index.html" target="_blank">Open Unit Heatmap</a>
      <a href="unit_tests/unit_coverage_report.txt" target="_blank">Unit Coverage Report</a>
      <a href="unit_tests/unit_heatmap.txt" target="_blank">Unit Heatmap Text</a>
    </div>
    <iframe src="unit_tests/heatmap_html/index.html" title="Unit Coverage"></iframe>
  </section>
  <section id="integration" class="panel">
    <div class="links">
      <a href="${first_version_token}/heatmap_html/index.html" target="_blank">Open Integration Heatmap</a>
      <a href="${first_version_token}/hotpaths_report.txt" target="_blank">Integration Coverage Report</a>
      <a href="${first_version_token}/heatmap.txt" target="_blank">Integration Heatmap Text</a>
      <a href="${first_version_token}/flame.svg" target="_blank">Flamegraph</a>
    </div>
    <iframe src="${first_version_token}/heatmap_html/index.html" title="Integration Coverage"></iframe>
  </section>
  <script>
    const tabs = document.querySelectorAll(".tab");
    const panels = document.querySelectorAll(".panel");
    tabs.forEach((tab) => tab.addEventListener("click", () => {
      tabs.forEach((x) => x.classList.remove("active"));
      panels.forEach((p) => p.classList.remove("active"));
      tab.classList.add("active");
      document.getElementById(tab.dataset.target).classList.add("active");
    }));
  </script>
</body>
</html>
EOF

printf '[%s] Done\n' "$(date +'%Y-%m-%d %H:%M:%S')"
printf '  output root: %s\n' "${PROFILE_ROOT}"
printf '\nDisplay dashboard:\n'
printf '  xdg-open "%s/index.html"\n' "${PROFILE_ROOT}"
printf '\nServe profiles in browser (coverage + flamegraphs):\n'
printf '  FIX_UID=%s FIX_GID=%s FIX_PROFILE_UI_PORT=%s docker compose -f "%s" --profile profiler-ui up --build -d fix-profiler-ui\n' \
    "${HOST_UID}" "${HOST_GID}" "${PROFILE_UI_PORT}" "${COMPOSE_FILE}"
printf '  xdg-open "http://localhost:%s/hotpaths_%s/index.html"\n' "${PROFILE_UI_PORT}" "${timestamp}"

PROFILE_COLLECTION_ROOT="$(dirname "${PROFILE_ROOT}")"
rm -rf "${PROFILE_COLLECTION_ROOT}/latest"
ln -s "hotpaths_${timestamp}" "${PROFILE_COLLECTION_ROOT}/latest"
cat > "${PROFILE_COLLECTION_ROOT}/index.html" <<'EOF'
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
