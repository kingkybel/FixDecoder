#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GEN_SCRIPT="${SCRIPT_DIR}/generate_realistic_fix_samples.py"

if [[ ! -f "${GEN_SCRIPT}" ]]; then
    echo "[warn] Realistic FIX sample generator not found at ${GEN_SCRIPT}. Continuing build." >&2
    exit 0
fi

set +e
"${GEN_SCRIPT}"
rc=$?
set -e

if [[ ${rc} -ne 0 ]]; then
    echo "[warn] Realistic FIX sample generation failed with exit ${rc}. Continuing build." >&2
fi

exit 0
