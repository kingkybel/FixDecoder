#!/usr/bin/env bash
set -euo pipefail

find_llvm_tool() {
  local base="$1"
  local candidate=""
  for candidate in \
    "${base}" \
    "${base}-21" "${base}-20" "${base}-19" "${base}-18" "${base}-17" \
    "${base}21" "${base}20" "${base}19" "${base}18" "${base}17"; do
    if command -v "$candidate" >/dev/null 2>&1; then
      command -v "$candidate"
      return 0
    fi
  done
  for candidate in /usr/lib/llvm*/bin/"${base}"; do
    if [ -x "$candidate" ]; then
      printf "%s\n" "$candidate"
      return 0
    fi
  done
  return 1
}

profdata_bin="$(find_llvm_tool llvm-profdata || true)"
cov_bin="$(find_llvm_tool llvm-cov || true)"

if [ -z "${profdata_bin}" ] || [ -z "${cov_bin}" ]; then
  if command -v apk >/dev/null 2>&1; then
    apk add --no-cache llvm >/dev/null
  elif command -v apt-get >/dev/null 2>&1; then
    apt-get update >/dev/null && apt-get install -y --no-install-recommends llvm >/dev/null
  elif command -v dnf >/dev/null 2>&1; then
    dnf -y install llvm >/dev/null
  fi
  profdata_bin="$(find_llvm_tool llvm-profdata || true)"
  cov_bin="$(find_llvm_tool llvm-cov || true)"
fi

if [ -z "${profdata_bin}" ] || [ -z "${cov_bin}" ]; then
  echo "llvm-profdata/llvm-cov not found in builder image" >&2
  exit 2
fi

shopt -s nullglob
profraw=(/profiles/raw/*.profraw)
if [ "${#profraw[@]}" -eq 0 ]; then
  echo "No profile files found in /profiles/raw" >&2
  exit 2
fi

"$profdata_bin" merge -sparse "${profraw[@]}" -o /profiles/merged.profdata
"$cov_bin" report /out/fix_controller_demo -instr-profile=/profiles/merged.profdata > /profiles/hotpaths_report.txt
"$cov_bin" show /out/fix_controller_demo -instr-profile=/profiles/merged.profdata -format=text > /profiles/heatmap.txt
"$cov_bin" show /out/fix_controller_demo -instr-profile=/profiles/merged.profdata -format=html -output-dir=/profiles/heatmap_html
"$cov_bin" export /out/fix_controller_demo -instr-profile=/profiles/merged.profdata > /profiles/coverage_export.json
