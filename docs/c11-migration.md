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
| Expressions | C lexer, bounded parser/coercion/comparison/string methods, typed variable/regex Host; old evaluator implementation removed | C context and compatible C regex provider replace the explicit temporary C++ adapter |
| Structured document queries | C JSONPath/YPATH/XPath/CSVPath dispatch and owned results; explicit temporary C++ value/exception bridge | Migrate remaining `WorkflowValue` callers, then delete the bridge |
| Workflow values | C exact numeric conversion, borrowed path reads, transactional mutations over the existing Salts JSON owner; temporary C++ container/exception facade | Migrate callers and remove the facade |
| YAML, contracts, effects, profiles, reviewed plans | C++ remains | One C-owned representation and unchanged SDK/YAML contracts |
| DAG, contexts, triggers, scheduler, actions | C++ application layer and explicit compatibility dispatch remain | Native C execution and atomic Parallel branch state |
| Shell/application/service adapters | C implementations exist, C++ conversions remain | C callers only; remove adapter headers/translation units |
| SDK, CLI, script wrapper, remaining tools/examples/tests | C++ remains | Preserve the C ABI and CLI behavior, port all consumers/tests |
| Root CMake/presets/install | Still loads C++ during migration | C-only compiler graph and installed C consumer link audit |

The first cut deletes six C++ source/header/test files. It does **not** meet the
whole-program completion gate yet. The baseline contained 186 tracked C++ files
(excluding `.hpp.in` templates); 181 remain, including a temporary facade regression
test added during the value port. This count is
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
The same project now builds the C evaluator and a second executable without a
C++ linker/runtime, `WorkflowContext` or Legacy Executor. It validates all tokens
before any Host call, then parses with one-token lookahead. It preserves eager
logical evaluation, scalar coercion, six-decimal numeric string conversion,
binary strings and legacy numeric prefix conversion. Strings use unique `tstr`
owners. Failure frees every intermediate value/regex handle and leaves caller
outputs unchanged; Host reads are not a transactional side-effect mechanism.

The evaluator's default limits (also used by the temporary application bridge)
are 64 KiB input, 16384 tokens including END, nesting depth 128 and 1 MiB strings.
Exhaustion fails closed; it does not truncate strings, produce a partial value
or dispatch through a hidden legacy path. Internal C callers may supply explicit
input/token/string limits and a depth up to 128. This intentionally bounds
previously unbounded C++ parsing; unusually large/deep expressions are rejected
with a diagnostic. The installed SDK ABI and ordinary expression syntax remain
unchanged. Scratch/value memory limits do not bound execution inside a Host
regex provider; that provider still owns its execution/resource policy.

Variable reads and regex compile/search/destroy are explicit synchronous typed
Host operations. The callback tuple must be complete. Every returned regex
handle, including a partial handle on failure, has one destruction owner.
The C core never contains or discovers a C++ context/regex implementation.
`expression_evaluator_legacy_bridge.cpp` temporarily supplies `WorkflowContext`
conversion, the original `std::regex` provider, public variant conversion and
the existing warning/failure boundary. It is not a native C regex implementation
and must be removed when its providers/callers migrate. The alternative of
substituting a smaller incompatible regex engine was rejected. Stateless
concurrent callers and reentrant evaluation from a Host callback have executable
C tests; the regex protocol mock tests ownership only, while real matching is
covered by the application suite. Original C++ evaluator algorithms are deleted.

`praktor/src/data/standalone` links the real Salts JSON/YAML/XML/CSV query
providers with a C compiler only. Query results own independent JSON trees;
borrowed input is unchanged and failure leaves the caller's output untouched.
Tests cover path defaults/prefixes, zero/one/many match shapes, CSV arrays and
duplicate headers, scalar types, exact byte lengths and malformed input/query
cleanup. Provider parsing and query limits remain in force; this DOM API makes
no allocation-free or fixed-capacity claim. The old C++ query implementation
has been removed; `structured_document_query_legacy_bridge.cpp` only converts
values and statuses for callers that have not migrated yet. Its existing
regressions now run with the script engine both enabled and disabled.

The same data project now builds the native workflow value library and two
additional C-only executables. The single state owner remains a Salts JSON root:
the C API operates on that owner, reuses its factories/cloning/serialization,
and returns borrowed path reads. No second JSON tree implementation or hidden
C++ environment is introduced. Exact signed/unsigned integer tokens retain all
64 bits; floating conversion checks finite integral values against exact,
exclusive power-of-two upper bounds. This corrects the old undefined conversion
at rounded `INT64_MAX`/`UINT64_MAX` boundaries: `9223372036854775808.0` and
`18446744073709551616.0` now fail with the existing range exception category.

Mutation paths accept at most 128 object keys and reject excess without changing
owners. This explicitly bounds previously unbounded Proxy paths. C keys preserve
embedded NULs; the temporary C++ facade deliberately retains its existing C-string
key semantics and length-aware string values. Existing null/scalar children
remain errors; only a null root is promoted. Self insertion retains the original
snapshot shape after creating parent objects, using a provisional root.

Ordinary writes prepare only the missing suffix and source, then perform one
checked adoption or root replacement. Cloning the whole root on every write was
rejected because of its cost for accumulating workflow state. A direct take
adopts the source owner. A take into a missing suffix first clones the source,
then releases its original owner only after commit: Salts JSON has no detach API
that could undo an adopted source after a failed suffix commit. Self writes that
need new parents use a provisional root clone to preserve legacy snapshot shape.
Failure discards provisional state and leaves root/source owners unchanged.
Erase builds its replacement before publishing it. Provider-boundary fault tests
fail each creation, clone and checked adoption in the actual C algorithm while
using the real JSON provider; they verify unchanged owners and serialized trees.
They do not claim to inject failures inside the provider's own allocator.
Four concurrent C callers also exercise independent owners with the real provider;
sharing a root across mutation/read calls still requires external synchronization.

`workflow_value_legacy_bridge.cpp` retains class/Proxy lifetimes, STL container
conversion, exception mapping and public string conversion for current callers;
its old numeric/path/mutation algorithms are deleted. It remains a temporary
C++ dependency of the application, not of the native C value target. Whole-program
C++ removal still requires migrating those callers and deleting this facade.

All three projects provide release/ASan presets. On Linux,
`bash cmake/ci/qualify-c-only-tools.sh release` configures/builds/tests all three
complete graphs and rejects C++ symbols/libraries in their executable links.
The complete-source TSan workflow invokes the same qualification with `thread`
against instrumented Salts and SaltsUtils. Ordinary platform CI continues
building/testing the application and installed SDK during migration.

Keep this follow-up Draft until the whole-program gates above pass. No
automatic merge or publication is authorized by the migration request. The
merged master stays on its accepted backend while this branch is incomplete.
