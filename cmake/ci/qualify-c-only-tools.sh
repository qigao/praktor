#!/usr/bin/env bash
# Linux qualification: full C-only build graphs, executable tests and ELF audit.
set -euo pipefail
mode=${1:-release}
flags=
link_flags=
case "$mode" in
  release) ;;
  address) flags='-fsanitize=address,undefined -fno-omit-frame-pointer -g'; link_flags=-fsanitize=address,undefined ;;
  thread) flags='-fsanitize=thread -fno-omit-frame-pointer -O1 -g'; link_flags=-fsanitize=thread ;;
  *) echo 'Expected release, address or thread' >&2; exit 2 ;;
esac
export LD_LIBRARY_PATH="$SALTS_ROOT/lib:$SALTS_UTILS_ROOT/lib:${LD_LIBRARY_PATH:-}"
for unit in schema lexer query; do
  case "$unit" in
    schema) directory=tools/schema/standalone ;;
    lexer) directory=praktor/src/expressions/standalone ;;
    query) directory=praktor/src/data/standalone ;;
  esac
  (
    cd "$directory"
    cmake --preset "$unit-c-release" "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_EXE_LINKER_FLAGS=$link_flags"
    cmake --build --preset "$unit-c-release"
    ctest --preset "$unit-c-release" --timeout 90
  )
done
for binary in build/schema-c-release/praktor-schema build/schema-c-release/schema_projection_test build/lexer-c-release/expression_lexer_test build/query-c-release/structured_document_query_c_test; do
  python3 cmake/ci/audit-c11-runtime-link.py "$binary"
done
