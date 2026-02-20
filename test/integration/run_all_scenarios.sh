#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE_FILE="${REPO_ROOT}/test/integration/compose.yml"
LOG_DIR="${REPO_ROOT}/test/integration/logs"
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_FILE:-${LOG_DIR}/run_all_scenarios_$(date +%Y%m%d_%H%M%S).log}"
TARGET_BASE_IMAGE="${FIX_BASE_IMAGE:-ubuntu:24.04}"
TARGET_COMPILER_FAMILY="${FIX_COMPILER_FAMILY:-g++}"
TARGET_COMPILER_VERSION="${FIX_COMPILER_VERSION:-}"
TARGET_IMAGE_SUFFIX=""
TARGET_BUILDER_IMAGE=""
TARGET_RUNTIME_IMAGE=""
BUILD_ONLY=0
REALISTIC_MESSAGES_DIR="${FIX_REALISTIC_MESSAGES_DIR:-/workspace/data/samples/realistic}"

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
  -o, --os <image>                  Base image for build/runtime containers.
                                     Examples: ubuntu:24.04, fedora:41
  -c, --compiler <name>             Compiler family: g++, gcc, or clang.
  -v, --compiler-version <major>    Preferred compiler major version (for example 14 or 18).
  -b, --build-only                  Build images + binary artifact, then exit.
  -h, --help                         Show this help.

Defaults:
  --os ubuntu:24.04
  --compiler g++
  --compiler-version (unset; uses latest available for selected compiler family)
EOF
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

sanitize_for_tag() {
    local raw="$1"
    local lowered
    lowered="$(printf '%s' "${raw}" | tr '[:upper:]' '[:lower:]')"
    lowered="${lowered//g++/gxx}"
    lowered="${lowered//clang++/clangxx}"
    # Keep tag-safe chars and normalize separators.
    lowered="$(printf '%s' "${lowered}" | tr '/:@ ' '-' | tr -cd 'a-z0-9_.-')"
    lowered="$(printf '%s' "${lowered}" | sed -E 's/-+/-/g; s/^-+//; s/-+$//')"
    echo "${lowered}"
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
            -o|--os|--base-image)
                shift
                if [[ $# -eq 0 || "$1" == -* ]]; then
                    echo "Missing value after --os/--base-image/-o" >&2
                    usage >&2
                    exit 2
                fi
                TARGET_BASE_IMAGE="$1"
                shift
                ;;
            -c|--compiler)
                shift
                if [[ $# -eq 0 || "$1" == -* ]]; then
                    echo "Missing value after --compiler/-c" >&2
                    usage >&2
                    exit 2
                fi
                local fam
                fam="$(normalize_compiler_family "$1")"
                if [[ -z "${fam}" ]]; then
                    echo "Unsupported compiler family: '$1' (use g++|gcc|clang)" >&2
                    usage >&2
                    exit 2
                fi
                TARGET_COMPILER_FAMILY="${fam}"
                shift
                ;;
            -v|--compiler-version)
                shift
                if [[ $# -eq 0 || "$1" == -* ]]; then
                    echo "Missing value after --compiler-version/-v" >&2
                    usage >&2
                    exit 2
                fi
                if [[ ! "$1" =~ ^[0-9]+$ ]]; then
                    echo "Compiler version must be numeric major version, got '$1'" >&2
                    usage >&2
                    exit 2
                fi
                TARGET_COMPILER_VERSION="$1"
                shift
                ;;
            -b|--build-only)
                BUILD_ONLY=1
                shift
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

    local compiler_tag
    local compiler_ver_tag
    compiler_tag="$(sanitize_for_tag "${TARGET_COMPILER_FAMILY}")"
    compiler_ver_tag="$(sanitize_for_tag "${TARGET_COMPILER_VERSION}")"
    if [[ -z "${compiler_ver_tag}" ]]; then
        compiler_ver_tag="latest"
    fi
    TARGET_IMAGE_SUFFIX="$(sanitize_for_tag "${TARGET_BASE_IMAGE}")-${compiler_tag}-${compiler_ver_tag}"
    TARGET_BUILDER_IMAGE="fix-controller-builder:${TARGET_IMAGE_SUFFIX}"
    TARGET_RUNTIME_IMAGE="fix-controller-runtime:${TARGET_IMAGE_SUFFIX}"
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
    if ! env \
        FIX_BASE_IMAGE="${TARGET_BASE_IMAGE}" \
        FIX_COMPILER_FAMILY="${TARGET_COMPILER_FAMILY}" \
        FIX_COMPILER_VERSION="${TARGET_COMPILER_VERSION}" \
        FIX_BUILDER_IMAGE="${TARGET_BUILDER_IMAGE}" \
        FIX_RUNTIME_IMAGE="${TARGET_RUNTIME_IMAGE}" \
        "$@" docker compose -f "${COMPOSE_FILE}" --profile "${profile}" up \
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
    env \
        FIX_BASE_IMAGE="${TARGET_BASE_IMAGE}" \
        FIX_COMPILER_FAMILY="${TARGET_COMPILER_FAMILY}" \
        FIX_COMPILER_VERSION="${TARGET_COMPILER_VERSION}" \
        FIX_BUILDER_IMAGE="${TARGET_BUILDER_IMAGE}" \
        FIX_RUNTIME_IMAGE="${TARGET_RUNTIME_IMAGE}" \
        docker compose -f "${COMPOSE_FILE}" build fix-exchange-1
    env \
        FIX_BASE_IMAGE="${TARGET_BASE_IMAGE}" \
        FIX_COMPILER_FAMILY="${TARGET_COMPILER_FAMILY}" \
        FIX_COMPILER_VERSION="${TARGET_COMPILER_VERSION}" \
        FIX_BUILDER_IMAGE="${TARGET_BUILDER_IMAGE}" \
        FIX_RUNTIME_IMAGE="${TARGET_RUNTIME_IMAGE}" \
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
log_line "Base image: ${TARGET_BASE_IMAGE}"
log_line "Compiler: ${TARGET_COMPILER_FAMILY} ${TARGET_COMPILER_VERSION:-latest}"
log_line "Builder image tag: ${TARGET_BUILDER_IMAGE}"
log_line "Runtime image tag: ${TARGET_RUNTIME_IMAGE}"
log_line "Compose file: ${COMPOSE_FILE}"
log_line "Realistic message dir: ${REALISTIC_MESSAGES_DIR}"
prepare_runtime

if [[ ${BUILD_ONLY} -eq 1 ]]; then
    log_line "Build-only mode enabled; skipping scenario execution."
    print_summary
    exit 0
fi

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
        FIX_REALISTIC_MESSAGES_DIR="${REALISTIC_MESSAGES_DIR}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_CLIENT_1_BEGIN_STRING="${version}"

    run_case "single-client: performance [${version}]" "single-client" "fix-client-1" \
        FIX_SCENARIO=performance \
        FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-40}" \
        FIX_PERF_PAYLOAD_SIZE="${FIX_PERF_PAYLOAD_SIZE:-512}" \
        FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
        FIX_REALISTIC_MESSAGES_DIR="${REALISTIC_MESSAGES_DIR}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_CLIENT_1_BEGIN_STRING="${version}"
done

echo "Running multi-* scenarios across FIX versions: ${FIX_VERSIONS[*]}"
for version in "${FIX_VERSIONS[@]}"; do
    run_case "multi-exchange: conversation [${version}]" "multi-exchange" "fix-client-multi" \
        FIX_SCENARIO=conversation \
        FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
        FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
        FIX_REALISTIC_MESSAGES_DIR="${REALISTIC_MESSAGES_DIR}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_EXCHANGE_2_BEGIN_STRING="${version}" \
        FIX_CLIENT_MULTI_BEGIN_STRING="${version}"

    run_case "multi-client: conversation [${version}]" "multi-client" "fix-client-2" \
        FIX_SCENARIO=conversation \
        FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
        FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
        FIX_REALISTIC_MESSAGES_DIR="${REALISTIC_MESSAGES_DIR}" \
        FIX_EXCHANGE_1_BEGIN_STRING="${version}" \
        FIX_EXCHANGE_2_BEGIN_STRING="${version}" \
        FIX_CLIENT_2_BEGIN_STRING="${version}"

    run_case "multi-mesh: conversation [${version}]" "multi-mesh" "fix-client-mesh-1" \
        FIX_SCENARIO=conversation \
        FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
        FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
        FIX_REALISTIC_MESSAGES_DIR="${REALISTIC_MESSAGES_DIR}" \
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
    FIX_REALISTIC_MESSAGES_DIR="${REALISTIC_MESSAGES_DIR}" \
    FIX_EXCHANGE_1_BEGIN_STRING="FIX.4.2" \
    FIX_EXCHANGE_2_BEGIN_STRING="FIXT.1.1" \
    FIX_CLIENT_MULTI_BEGIN_STRING="FIX.4.4"

run_case "multi-client: mixed protocol selection B" "multi-client" "fix-client-2" \
    FIX_SCENARIO=conversation \
    FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
    FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
    FIX_REALISTIC_MESSAGES_DIR="${REALISTIC_MESSAGES_DIR}" \
    FIX_EXCHANGE_1_BEGIN_STRING="FIX.4.0" \
    FIX_EXCHANGE_2_BEGIN_STRING="FIX.4.4" \
    FIX_CLIENT_2_BEGIN_STRING="FIXT.1.1"

run_case "multi-mesh: mixed protocol selection C" "multi-mesh" "fix-client-mesh-1" \
    FIX_SCENARIO=performance \
    FIX_CONVERSATION_MESSAGES="${FIX_CONVERSATION_MESSAGES:-30}" \
    FIX_PERF_PAYLOAD_SIZE="${FIX_PERF_PAYLOAD_SIZE:-512}" \
    FIX_RUNTIME_SECONDS="${FIX_RUNTIME_SECONDS:-30}" \
    FIX_REALISTIC_MESSAGES_DIR="${REALISTIC_MESSAGES_DIR}" \
    FIX_EXCHANGE_1_BEGIN_STRING="FIX.4.3" \
    FIX_EXCHANGE_2_BEGIN_STRING="FIXT.1.1" \
    FIX_CLIENT_MESH_1_BEGIN_STRING="FIX.4.1" \
    FIX_CLIENT_MESH_2_BEGIN_STRING="FIX.4.4"

echo
log_line "All FIX controller docker scenario suites passed."
print_summary
