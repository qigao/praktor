#!/usr/bin/env bash
# Qualify the opt-in C11 Process/Workflow lifecycle without rebuilding every
# native dependency. This is a consumer-side ASan/UBSan or TSan slice, not
# proof that an uninstrumented installed NativeIO provider is race-free.
set -euo pipefail

mode="${1:?usage: qualify-c11-lifetime-sanitizers.sh address|thread}"
case "$mode" in
  address)
    sanitizer_flags="-fsanitize=address,undefined"
    ;;
  thread)
    sanitizer_flags="-fsanitize=thread"
    ;;
  *)
    echo "Unsupported C11 sanitizer mode: $mode" >&2
    exit 2
    ;;
esac

: "${CMAKE_PRESET:?CMAKE_PRESET is required}"
build_dir="build/ci-c11-${mode}"
compile_flags="${sanitizer_flags} -fno-omit-frame-pointer -fno-optimize-sibling-calls -O1 -g"

cmake --preset "$CMAKE_PRESET" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_TESTS=ON \
  -DENABLE_SCRIPT_ENGINE=OFF \
  -DPRAKTOR_ENABLE_C11_ACTIONS=ON \
  "-DCMAKE_C_FLAGS=$compile_flags" \
  "-DCMAKE_CXX_FLAGS=$compile_flags" \
  "-DCMAKE_EXE_LINKER_FLAGS=$sanitizer_flags" \
  "-DCMAKE_SHARED_LINKER_FLAGS=$sanitizer_flags"

cmake --build "$build_dir" \
  --target actions_c11_tree_executor_session_test core_only_workflow_test.praktor_lib \
  --parallel 2
ctest --test-dir "$build_dir" \
  --no-tests=error --output-on-failure --parallel 1 --timeout 75 \
  -R '^(actions_c11_tree_executor_session_test|core_only_workflow_test\.praktor_lib)$'
