# Complete C11 migration

The follow-up to merged PR #78 replaces all remaining first-party C++ in
Praktor. This is a whole-program migration, including runtime, public build
interfaces, CLI, SDK, maintenance tools, tests and examples. It is not complete
when only an optional core can link without C++.

## Required result

- Root, standalone and installed-package builds use `LANGUAGES C`. Production
  targets, tools and tests neither compile C++ nor require a C++ linker/runtime.
  First-party `.cpp`, `.hpp` and other C++ source/header/template files are
  removed after their consumers and behavior have moved to C. `extern "C"`
  guards in C-compatible public headers remain valid for embedding clients.
- Preserve the installed C ABI, capability reporting, JSON output, YAML syntax,
  workflow contracts, reviewed plan identity, paths, environment, task results,
  events and HostTool protocols. Preserve enabled script functionality through
  the provider's C API. Removing a feature or its tests does not count as a
  successful migration.
- Replace Legacy execution with the native C path only after supported leaves
  and application workflows have parity coverage. Delete the compatibility
  adapter and backend selector at the end; an opt-in C core plus mandatory
  C++ WorkflowRunner is not the final product.
- Preserve Salts::Process and its ownership/retainer contract. All consumers
  detach borrowed callbacks before transfer; outstanding leases keep their C
  owner until acknowledged completion. Cancellation, timeout, failed close and
  recovery must not free live state or invent successful completion.
- Parallel branches use independent state and one atomic group commit. Capacity
  failure, conflict, cancellation or a failed branch discards uncommitted state;
  sequential partial commits are forbidden.
- Run the complete configured build graph and real tests. Keep TinyTest and
  curl-based GitHub package downloads; do not install Mono or restore a NuGet
  executable dependency. Structural source/build audits complement executable
  parity and link tests; they cannot prove workflow behavior by themselves.

## Architecture and migration order

The existing C plan, Session, bounded value store, output scanner, Process
driver/worker, application controller and service controller are retained.
The stable SDK remains the application boundary. A shared C workflow value,
plan and context owner must become the single source of truth used by the SDK,
CLI and leaf hosts; do not add an independent second workflow implementation.
Use Salts' public C containers, strings, filesystem, IDL/Schema and provider APIs
at their existing ownership boundaries.

Keeping optional C++ facades as the permanent implementation was considered
and rejected because it does not meet the new C-only build requirement.
Deleting the old implementation before replacement parity was also rejected:
it would silently remove workflow features and their independent regression
reference. Port ownership and behavior by module, move callers to each C API,
then remove that module's C++ implementation/tests. The accepted master remains
the rollback point; partial migration is not released as the completed product.

| Area | Current migration state | Required next state |
| --- | --- | --- |
| Schema maintenance tool | C projection, CLI and TinyTest tests; no `WorkflowValue`, `praktor_lib` or C++ link | Preserve generated grammar and all existing refusal rules |
| Expression Lexer | C iterator and decoder; removed original C++ implementation/header | Native evaluator consumes C values with bounded parse state |
| Values, YAML, contracts, effects, profiles, reviewed plans | C++ remains | One C-owned representation and unchanged SDK/YAML contracts |
| DAG, contexts, triggers, scheduler, actions | C++ application layer and explicit compatibility dispatch remain | Native C execution and atomic Parallel branch state |
| Shell/application/service adapters | C implementations exist, C++ conversions remain | C callers only; remove adapter headers/translation units |
| SDK, CLI, script wrapper, remaining tools/examples/tests | C++ remains | Preserve the C ABI and CLI behavior, port all consumers/tests |
| Root CMake/presets/install | Still loads C++ during migration | C-only compiler graph and installed C consumer link audit |

The first cut deletes six C++ source/header/test files. It does **not** meet the
whole-program completion gate yet. The baseline contained 186 tracked C++ files
(excluding `.hpp.in` templates); 180 remain after this cut. This count is
inventory, not a measure of runtime parity or a substitute for the final audit.

Native regex support must preserve the established matching language or gain
an explicitly reviewed contract change. Do not replace regex with substring
matching to make a leaf appear supported. New public ABI behavior, provider
choices and capability changes need their own implementation and tests before
the old path can be removed.

## Current executable evidence

`tools/schema/standalone` is a C-only project using the installed
Salts IDL/Schema/JsonParser APIs. CTest runs the real CLI, compares the generated
schema with the checked-in artifact, covers aliases/presence/defaults/constraints,
refusal cases and unchanged borrowed policies. Projection clones editor input
and publishes an owned result only after complete success. Merge/shape nesting
is limited to 128. JSON allocation is explicit; no allocation-free claim is made.

`praktor/src/expressions/standalone` is a C-only project using the base Salts
SDK. Lexer iteration allocates nothing, preserves token positions and escape
rules, and allows a buffer-size retry without consuming input. Binary quoted
values retain their lengths. Legacy unterminated literals and slash heuristics
are deliberately retained until an evaluator-level syntax change is reviewed.
The C++ evaluator temporarily converts tokens to its existing value containers;
its remaining regex/parser/value behavior is not claimed to have moved to C.

Both projects provide release/ASan presets. On Linux,
`bash cmake/ci/qualify-c-only-tools.sh release` configures/builds/tests both
complete graphs and rejects C++ symbols/libraries in their executable links.
The complete-source TSan workflow invokes the same qualification with `thread`
against instrumented Salts and SaltsUtils. Ordinary platform CI continues
building/testing the application and installed SDK during migration.

Keep this follow-up Draft until the whole-program gates above pass. No
automatic merge or publication is authorized by the migration request. The
merged master stays on its accepted backend while this branch is incomplete.
