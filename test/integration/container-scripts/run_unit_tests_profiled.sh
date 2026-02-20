#!/usr/bin/env bash
set -euo pipefail

mkdir -p /profiles/raw
LLVM_PROFILE_FILE="${UNIT_PROFILE_PATTERN:-/profiles/raw/unit-%p.profraw}" /out/run_tests > /profiles/unit_test_output.txt
