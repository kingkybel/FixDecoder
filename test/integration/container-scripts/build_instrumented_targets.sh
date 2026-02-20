#!/usr/bin/env bash
set -euo pipefail

cc_bin=clang
cxx_bin=clang++
if [ -n "${FIX_COMPILER_VERSION:-}" ]; then
  if command -v "clang-${FIX_COMPILER_VERSION}" >/dev/null 2>&1; then cc_bin="clang-${FIX_COMPILER_VERSION}"; fi
  if command -v "clang++-${FIX_COMPILER_VERSION}" >/dev/null 2>&1; then cxx_bin="clang++-${FIX_COMPILER_VERSION}"; fi
fi

rm -rf /tmp/build
cmake -S /workspace -B /tmp/build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER="${cc_bin}" \
  -DCMAKE_CXX_COMPILER="${cxx_bin}" \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O2 -g -fno-omit-frame-pointer -fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g -fno-omit-frame-pointer -fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_EXE_LINKER_FLAGS="-static -fprofile-instr-generate"
cmake --build /tmp/build --target fix_controller_demo run_tests --parallel 3
cp /tmp/build/RelWithDebInfo/bin/fix_controller_demo /out/fix_controller_demo
cp /tmp/build/RelWithDebInfo/bin/run_tests /out/run_tests
chmod +x /out/fix_controller_demo /out/run_tests
