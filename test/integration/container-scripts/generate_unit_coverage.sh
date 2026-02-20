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
  echo "No profile files found in /profiles/raw for unit tests" >&2
  exit 2
fi

mapfile -t project_sources < <(
  find /workspace/src /workspace/include -type f \
    \( -name "*.cc" -o -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.hpp" \) \
    | sort
)
if [ "${#project_sources[@]}" -eq 0 ]; then
  echo "No project sources found under /workspace/src or /workspace/include" >&2
  exit 2
fi

cov_filter_regex='(^|.*/)(tmp/build/|build/_deps/|_deps/|googletest|gtest|tinyxml2|/workspace/test/|test/|/usr/include/|usr/include/)'

"$profdata_bin" merge -sparse "${profraw[@]}" -o /profiles/merged.profdata
"$cov_bin" report /out/run_tests -instr-profile=/profiles/merged.profdata -ignore-filename-regex="${cov_filter_regex}" "${project_sources[@]}" > /profiles/unit_coverage_report.txt
"$cov_bin" show /out/run_tests -instr-profile=/profiles/merged.profdata -ignore-filename-regex="${cov_filter_regex}" -format=text "${project_sources[@]}" > /profiles/unit_heatmap.txt
"$cov_bin" show /out/run_tests -instr-profile=/profiles/merged.profdata -ignore-filename-regex="${cov_filter_regex}" -format=html -output-dir=/profiles/heatmap_html "${project_sources[@]}"
"$cov_bin" export /out/run_tests -instr-profile=/profiles/merged.profdata -ignore-filename-regex="${cov_filter_regex}" "${project_sources[@]}" > /profiles/unit_coverage_export.json
