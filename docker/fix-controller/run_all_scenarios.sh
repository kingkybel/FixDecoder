#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE_FILE="${REPO_ROOT}/docker/fix-controller/compose.yml"
LOG_DIR="${REPO_ROOT}/docker/fix-controller/logs"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_FILE:-${LOG_DIR}/run_all_scenarios_$(date +%Y%m%d_%H%M%S).log}"

ALL_FIX_VERSIONS=(
    "FIX.4.0"
    "FIX.4.1"
    "FIX.4.2"
    "FIX.4.3"
    "FIX.4.4"
    "FIXT.1.1"
)
FIX_VERSIONS=()

cleanup() {
    docker compose -f "${COMPOSE_FILE}" down --remove-orphans >/dev/null 2>&1 || true
}

trap cleanup EXIT

TOTAL_CASES=0
PASSED_CASES=0
FAILED_CASES=0
CURRENT_CASE=""

ts() {
    date +'%Y-%m-%d %H:%M:%S'
}

log_line() {
    local line="$1"
    printf '[%s] %s\n' "$(ts)" "${line}" | tee -a "${LOG_FILE}"
}

print_summary() {
    log_line "Summary: total=${TOTAL_CASES} passed=${PASSED_CASES} failed=${FAILED_CASES}"
    log_line "Log file: ${LOG_FILE}"
}

usage() {
    cat <<'EOF'
Usage: run_all_scenarios.sh [options]

Options:
  -f, --fix-versions <version...>   Filter FIX versions to test.
                                     Accepts full version strings or shorthand.
                                     Examples:
                                       -f FIX.4.4
                                       -f 11 Fix-40
                                       --fix-versions fixt11 4.2
  -h, --help                         Show this help.
EOF
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

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help)
                usage
                exit 0
                ;;
            -f|--fix-versions)
                shift
                if [[ $# -eq 0 || "$1" == -* ]]; then
                    echo "Missing value after --fix-versions/-f" >&2
                    usage >&2
                    exit 2
                fi
                while [[ $# -gt 0 && "$1" != -* ]]; do
                    local normalized
                    normalized="$(normalize_fix_version "$1")"
                    if [[ -z "${normalized}" ]]; then
                        echo "Unsupported FIX version token: '$1'" >&2
                        usage >&2
                        exit 2
                    fi
                    if ! contains_version "${normalized}" "${FIX_VERSIONS[@]}"; then
                        FIX_VERSIONS+=("${normalized}")
                    fi
                    shift
                done
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
}

run_case() {
    local name="$1"
    local profile="$2"
    local exit_service="$3"
    shift 3

    CURRENT_CASE="${name}"
    TOTAL_CASES=$((TOTAL_CASES + 1))
    echo
    log_line "START ${name}"
    log_line "profile=${profile} exit_code_from=${exit_service} env=[$*]"
    docker compose -f "${COMPOSE_FILE}" down --remove-orphans >/dev/null 2>&1 || true
    local case_start
    case_start="$(date +%s)"
    local rc=0
    if ! env "$@" docker compose -f "${COMPOSE_FILE}" --profile "${profile}" up \
        --abort-on-container-failure \
        --exit-code-from "${exit_service}"; then
        rc=$?
    fi
    docker compose -f "${COMPOSE_FILE}" down --remove-orphans >/dev/null 2>&1 || true
    local case_end
    case_end="$(date +%s)"
    local elapsed=$((case_end - case_start))

    if [[ ${rc} -eq 0 ]]; then
        PASSED_CASES=$((PASSED_CASES + 1))
        log_line "PASS ${name} duration=${elapsed}s"
        return 0
    fi

    FAILED_CASES=$((FAILED_CASES + 1))
    log_line "FAIL ${name} duration=${elapsed}s exit_code=${rc}"
    print_summary
    return ${rc}
}

prepare_runtime() {
    log_line "Preparing runtime image and binary artifact (build once)"
    docker compose -f "${COMPOSE_FILE}" down --remove-orphans >/dev/null 2>&1 || true
    docker compose -f "${COMPOSE_FILE}" build fix-exchange-1
    docker compose -f "${COMPOSE_FILE}" --profile build-bin up \
        --build \
        --abort-on-container-failure \
        --exit-code-from fix-builder \
        fix-builder
    docker compose -f "${COMPOSE_FILE}" down --remove-orphans >/dev/null 2>&1 || true
    log_line "Build artifact ready in volume fix-controller-bin"
}

parse_args "$@"

log_line "Starting full docker FIX scenario suite"
log_line "FIX versions under test: ${FIX_VERSIONS[*]}"
log_line "Compose file: ${COMPOSE_FILE}"
prepare_runtime

echo "Running single-client scenarios across FIX versions: ${FIX_VERSIONS[*]}"
for version in "${FIX_VERSIONS[@]}"; do
    run_case "single-client: handshake [${version}]" "single-client" "fix-client-1" \
        FIX_SCENARIO=handshake \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_CLIENT_1_BEGIN_STRING="${version}"

    run_case "single-client: out_of_sync [${version}]" "single-client" "fix-client-1" \
        FIX_SCENARIO=out_of_sync \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_CLIENT_1_BEGIN_STRING="${version}"

    run_case "single-client: garbled [${version}]" "single-client" "fix-client-1" \
        FIX_SCENARIO=garbled \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_CLIENT_1_BEGIN_STRING="${version}"

    run_case "single-client: conversation [${version}]" "single-client" "fix-client-1" \
        FIX_SCENARIO=conversation \
        FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-40}" \
        FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_CLIENT_1_BEGIN_STRING="${version}"

    run_case "single-client: performance [${version}]" "single-client" "fix-client-1" \
        FIX_SCENARIO=performance \
        FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-40}" \
        FIX_PERF_PAYLOAD_SIZE="${FIX_PERF_PAYLOAD_SIZE:-512}" \
        FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_CLIENT_1_BEGIN_STRING="${version}"
done

echo "Running multi-* scenarios across FIX versions: ${FIX_VERSIONS[*]}"
for version in "${FIX_VERSIONS[@]}"; do
    run_case "multi-exchange: conversation [${version}]" "multi-exchange" "fix-client-multi" \
        FIX_SCENARIO=conversation \
        FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
        FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_EXCHANGE_2_BEGIN_STRING="${version}" \
        FIX_CLIENT_MULTI_BEGIN_STRING="${version}"

    run_case "multi-client: conversation [${version}]" "multi-client" "fix-client-2" \
        FIX_SCENARIO=conversation \
        FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
        FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_EXCHANGE_2_BEGIN_STRING="${version}" \
        FIX_CLIENT_2_BEGIN_STRING="${version}"

    run_case "multi-mesh: conversation [${version}]" "multi-mesh" "fix-client-mesh-1" \
        FIX_SCENARIO=conversation \
        FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
        FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_EXCHANGE_2_BEGIN_STRING="${version}" \
        FIX_CLIENT_MESH_1_BEGIN_STRING="${version}" \
        FIX_CLIENT_MESH_2_BEGIN_STRING="${version}"
done

echo "Running selected mixed-version multi-party scenarios"
run_case "multi-exchange: mixed protocol selection A" "multi-exchange" "fix-client-multi" \
    FIX_SCENARIO=conversation \
    FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
    FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
    FIX_EXCHANGE_1_BEGIN_STRING="FIX.4.2" \
    FIX_EXCHANGE_2_BEGIN_STRING="FIXT.1.1" \
    FIX_CLIENT_MULTI_BEGIN_STRING="FIX.4.4"

run_case "multi-client: mixed protocol selection B" "multi-client" "fix-client-2" \
    FIX_SCENARIO=conversation \
    FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
    FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
    FIX_EXCHANGE_1_BEGIN_STRING="FIX.4.0" \
    FIX_EXCHANGE_2_BEGIN_STRING="FIX.4.4" \
    FIX_CLIENT_2_BEGIN_STRING="FIXT.1.1"

run_case "multi-mesh: mixed protocol selection C" "multi-mesh" "fix-client-mesh-1" \
    FIX_SCENARIO=performance \
    FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
    FIX_PERF_PAYLOAD_SIZE="${FIX_PERF_PAYLOAD_SIZE:-512}" \
    FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
    FIX_EXCHANGE_1_BEGIN_STRING="FIX.4.3" \
    FIX_EXCHANGE_2_BEGIN_STRING="FIXT.1.1" \
    FIX_CLIENT_MESH_1_BEGIN_STRING="FIX.4.1" \
    FIX_CLIENT_MESH_2_BEGIN_STRING="FIX.4.4"

echo
log_line "All FIX controller docker scenario suites passed."
print_summary
