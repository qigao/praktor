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
| YAML documents, fields, structured values and system parameters | C source/document owner, node access, scalar/int conversion, bounded field readers, key whitelist validation and owned contract/HostTool value conversion, Download/Service/ManagedProcess/Command/Program parameter construction, shared string containers and Each/Triggers/DynamicTasks/Uses construction; explicit temporary C++ iterator/exception facade | Port remaining YAML task/workflow field construction, then delete the facade |
| Harness profile | Pure C reason-mask policy; temporary input/result conversion in the reviewed-plan caller | Preserve reason order and remove remaining caller conversion with the plan migration |
| Contracts, effects, reviewed plans | C++ remains | One C-owned representation and unchanged SDK/YAML contracts |
| DAG, contexts, triggers, scheduler, actions | C++ application layer and explicit compatibility dispatch remain | Native C execution and atomic Parallel branch state |
| Shell/application/service adapters | C implementations exist, C++ conversions remain | C callers only; remove adapter headers/translation units |
| SDK, CLI, script wrapper, remaining tools/examples/tests | C++ remains | Preserve the C ABI and CLI behavior, port all consumers/tests |
| Root CMake/presets/install | Still loads C++ during migration | C-only compiler graph and installed C consumer link audit |

The first cut deletes six C++ source/header/test files. It does **not** meet the
whole-program completion gate yet. The baseline contained 186 tracked C++ files
(excluding `.hpp.in` templates); 179 remain, including a temporary facade regression
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

YAML documents now have a separate C target linking only Salts Core and the
existing YAML provider. Its opaque owner copies source into `tstr`, retains that
buffer for the provider's borrowed spans, and frees parser nodes before source.
Moving the owner pointer preserves borrowed node handles; destroying it invalidates
them. Returned scalar/key/location text has independent `tstr` ownership. Bounded
borrowed diagnostic buffers also report a typed failure origin. The temporary
facade retains `bad_alloc` for owner/text allocation and the original ordinary
exception for a provider failure, including provider-reported memory exhaustion;
this preserves SDK out-of-memory classification without inspecting message text.
Cross-language fault tests exercise both error categories in the actual bridge.
Failed
construction or decoding publishes no partial owner, node or text output.

The provider keeps its default parsing policy and duplicate-key refusal. Sources
larger than its 32-bit span representation are rejected before copying. Alias
resolution is limited to 128 hops, closing the old unbounded nontrivial alias-cycle
loop; unresolved/self-target alias metadata retains its previous non-scalar
behavior. Location lookup preserves the existing 32-level search and mapping-key
preference, now with iterative C traversal. Strict decoded decimal `int` conversion
retains quoted integers, rejects plus/whitespace/base prefixes/fractions, checks
range and leaves outputs unchanged on failure. Optional integer 0/1 booleans use
the same provider semantics as before.

The supported public provider API supplies decoded C strings without lengths.
The migration therefore deliberately retains truncation at a decoded NUL and
raw-span `cyaml_get` key lookup, including its separate decoded-key location search.
Switching to raw spans for scalar decoding, calling an unpublished length API,
or replacing the YAML parser was rejected: those would alter the accepted grammar,
escape/folding behavior or installed-provider contract. `yaml_document.cpp` is
replaced by the C implementation; `yaml_document_legacy_bridge.cpp` contains only
class lifetimes, iterator/optional/string conversion and exception mapping for
field parsers awaiting migration. The source owner is no longer C++ state.

Executable C tests exercise independent source lifetime, pointer transfer,
aliases, decoding, strict numeric/boolean semantics, diagnostics, admission bounds,
concurrent independent documents and every module allocation/provider boundary.
Fault tests also verify parser destruction before retained-source release and
unchanged outputs. These inject module-boundary failures while using the actual
provider; they do not simulate its internal allocator or replace its algorithms.
The shared task field readers now run in the same C target: decoded scalars have
the existing 10 KiB limit; system Action strings additionally require the real
provider's string kind. Strict boolean fields accept case-insensitive
true/yes/1 and false/no/0 without trimming. Only `force_terminate` accepts on/off,
then provider semantic integer 0/1, then its original signed/base-prefixed and
underscore compatibility rule. The latter uses checked `uint64_t` accumulation
over a borrowed view, without building a C++ digit string. Lowercase prefixes,
negative zero, separator placement, overflow and full consumption remain explicit
rules; the field reader folds case before invoking this pure predicate.

Field failures own complete `tstr` diagnostics, preserving arbitrary caller
messages and key-preferred locations without fixed-buffer truncation. Callers
initialize/dispose a unique error owner, disjoint from inputs and value outputs;
each read clears the prior diagnostic. Diagnostic allocation failure is reported
as owned allocation, while provider decode failure keeps its provider category.
Successful text is independently owned; every failure leaves value outputs
unchanged. The 10 KiB field bound does not bound the provider's document/decoding
allocation. Replacing decoded strings with raw spans to reject earlier was
rejected because escapes, folding and decoded-NUL behavior would change.

The old inline C++ scalar, system string and boolean algorithms have been deleted.
Their temporary wrappers in the existing document bridge perform only string,
exception and typed policy conversion. Independent C tests consume the existing
34-row force-terminate corpus and 24 applicable system-string rows (the missing
executable rule remains a task-level test), exercise the exact length boundary,
aliases, long/binary errors, unchanged failed outputs and concurrent owners.
Provider-boundary injection covers decoding, text/message/location allocation
and rejected-value cleanup in the actual C implementation. Application bridge
tests also retain provider-vs-owned exception mapping and complete diagnostics.
A separate C YAML-value target now converts contract defaults/enums and typed
HostTool arguments directly into the existing Salts JSON owner. The document and
scalar-field target retains its original Core/YAML-only dependency; only value
conversion adds the JSON provider. Mapping values are converted before decoding
keys, preserving C++17 right-hand-side error precedence. Checked provider adoption
transfers each independent child once; failure frees child/provisional roots and
leaves caller output unchanged. Repeated aliases produce independent JSON trees.

Scalar conversion preserves provider string classification, exact signed decimal
`int64_t` before the original full-consumption `strtod` rule, case folding, decoded
NUL truncation and last decoded duplicate key wins. Provider null-node refusal is
retained. Structured contract strings keep their previous length policy rather
than acquiring the ordinary 10 KiB field limit. JSON factories and checked
adoption failures retain ordinary provider exceptions and existing diagnostics;
owned diagnostic/text allocation still maps to `bad_alloc` in the temporary bridge.

Conversion is limited to depth 128 (root depth 1) and 16384 expanded values,
including alias copies. Keys do not count as values. This intentionally bounds
previously unbounded recursion/alias expansion: excess fails closed with a source
location and no partial output. Traversal limits do not bound provider DOM/string
allocation bytes. An unbounded recursive port or a hidden C++ retry was rejected;
recursive and exponentially expanded aliases now have executable refusal tests.
The public installed SDK ABI remains unchanged; unusually deep/expanded contract
inputs are an explicit admission change on this migration branch.

The C key whitelist compares borrowed exact views against decoded keys in source
order and reports the first unknown key with its key location. It preserves
case sensitivity, decoded-NUL truncation, long error text and non-map no-op behavior.
Small immutable task-schema lists use a linear scan (keys times allowed keys),
avoiding a new hash/container dependency. The temporary C++ bridge marshals the
existing sets to borrowed views; no performance improvement is claimed. Both
original C++ conversion/validation algorithms have been deleted. Their wrappers
only transfer JSON ownership, marshal views and map exceptions.

Native tests cover nested values, independent source/alias/result lifetimes,
exact numeric tokens, duplicate decoded keys, large strings, admission edges,
concurrent conversions, key order and full diagnostics. Fault injection walks
real decode/copy/factory/adoption/message/location boundaries and checks unchanged
outputs and failure categories; an observation test verifies value-before-key
decode order. SDK contract and HostTool API tests join lifecycle ASan and complete-
source TSan filters to qualify these actual consumers, alongside platform suites.
Remaining YAML task/workflow construction and application execution still require
migration; these cuts do not remove that C++ layer.

Download, Service and ManagedProcess now construct parameters in a separate C11
`praktor_yaml_action_params` target. It links the existing C document/field target
and Salts CSTL; the shared Process header supplies the existing operation enum,
without linking a Process backend or Legacy Executor. An opaque unique owner
copies each retained `tstr` and owns a raw CSTL vector of argument pointers. It
explicitly frees each string before destroying vector storage. No YAML document,
C++ runtime, or execution context is retained. Typed views and indexed argument
views borrow this owner, remain valid across pointer transfer, and expire on
owner destruction. Independent owners can be read concurrently; destruction may
not race a read of the same owner.

Creation builds provisional state, validates each field in the original order,
and publishes the owner only on success. Failures destroy all provisional state
and leave the caller's output untouched. Existing scalar/string distinctions,
case-sensitive operations, defaults, strict decimal `int` timeouts, decoded-NUL
truncation, force-terminate compatibility grammar, and full location diagnostics
are preserved. Owned allocations retain their allocation failure category;
provider decode failures retain the provider category at the temporary exception
boundary. The application wrappers now only copy typed views into existing
C++ structures; the original system validation/construction algorithms are removed.

This branch adds admission limits of 4096 arguments and 1 MiB of retained decoded
text including NUL terminators, in addition to the existing 10 KiB per-field
limit. Defaults borrowed from static literals consume no owned text budget.
Excess input fails closed with a location diagnostic. These limits cover retained
parameter state, not the provider DOM, temporary decoding, or allocator overhead.
An unbounded vector port, parallel C++ parameter owner, and hidden Legacy retry
were rejected: each would undermine bounded ownership or maintain two sources of
truth. The temporary C++ copy remains an explicit migration cost until task nodes
and their callers are migrated; rolling back this cut must restore constructors
and their wiring together, without a runtime fallback.

Native TinyTest cases cover all 25 system-string and 34 force-terminate corpus
rows, defaults, validation precedence, aliases, source/owner lifetimes, concurrent
owners, exact argument/retained-byte boundaries and unchanged outputs. Allocation
fault cases walk owner allocation, real YAML decode/copy, CSTL append and error
construction boundaries. A native integration test passes the typed result into
the actual C Process controller with an explicit test Host; this proves the call
boundary, not OS-process backend parity. The independent executable links no C++
implementation. Application projection and typed-exception tests exercise the
actual temporary wrappers, and both new C tests join lifecycle ASan and complete-
source TSan qualification. Other YAML task/workflow construction and application
execution still require migration.

Command and Program now use the same C-owned parameter boundary. Command retains
its scalar/sequence distinction and typed Regex/JSON/Lines/KeyValue postprocessor
configuration; Program owns its executable, scalar-or-sequence arguments and
stdin. Internal enum/union views carry this parsing policy, without claiming a
new execution capability. Existing strings, defaults and integer capture groups
(including zero/negative values) are preserved. Output format remains case-folded
without trimming. Command checks parser conflicts in the original fixed order,
before decoding commands; sequence entries all decode before empty-entry checks.
Program keeps an empty scalar argument as no arguments while preserving empty
sequence entries. Plain scalar conversion errors retain their original message,
rather than acquiring an additional Parse-error prefix or string-kind guard.

The owner bounds both Command sequences and Program argument lists to 4096
entries and their aggregate retained text to 1 MiB including terminators. These
two constructors previously had no individual 10 KiB restriction; that system-
parameter guard is not imposed on commands, stdin or postprocessor fields.
Required-field/key validation, source locations, alias/NUL behavior and failure
origins reuse the existing C document/field mechanisms. Default delimiters and
separators borrow static literals. New capacity failures discard all provisional
strings and vector storage. No additional owner, container, parser,
retry path or external dependency is introduced. Splitting a second owner for
these constructors was rejected because it would duplicate the same lifetime
and admission policy; old C++ construction algorithms are removed and only
explicit typed-to-STL copying remains until task representations migrate.

Native regression cases cover the two constructors and all four postprocessors,
validation precedence, scalar/list coercion, aliases, immutable owner lifetimes,
concurrent independent owners and exact list/text admission edges. Fault walks
cover each parser branch, CSTL append, stdin/format conversion and early/late
refusals. A test sends typed KeyValue configuration into the actual C output
scanner, alongside the existing real C Process-controller boundary test. Both
native tests and the temporary-wrapper/exception tests remain in the current
platform, lifecycle sanitizer and complete-source TSan qualification; no test
selection or configured build graph is narrowed.

Shared scalar/sequence string lists, string maps, Each, Triggers, DynamicTasks
and Uses now construct in the separate C-only `praktor_yaml_task_fields` target.
The separate boundary owns task metadata and nested dimension lists without
pulling in Process or JSON providers; reusing the action owner would couple
these unrelated dependencies and field lifetimes. It uses existing diagnostic,
lookup and scalar helpers. C++ callers for task/workflow dependencies, variables,
environment, dotEnv, sources/generates and these four task blocks now copy typed
borrowed views only. The old constructors/validation algorithms are deleted.
Orchestration construction and task/workflow representation remain C++.

A unique opaque owner retains copied `tstr` data and raw CSTL vectors. Maps use
a CSTL tree index borrowing immutable owned keys plus an insertion-ordered entry
vector. Tree values are entry indexes, so vector relocation cannot invalidate
them. The index is destroyed before entry keys; matrix entries uniquely own
separately allocated lists, never byte-copied container ownership. Replacing a
decoded key frees superseded values/lists and keeps its first insertion position.
This preserves the existing unordered-map projection's iteration and Each
expansion order. No document, context or Executor survives construction; views
remain valid across owner pointer transfer and expire on owner destruction.
Failure destroys provisional state and leaves outputs unchanged, including index
insertion failure after entry ownership has transferred to the vector.

The branch-local admission policy is 4096 entries per list or distinct-key map,
16384 expanded data strings across a construction (including overwritten keys/values
and repeated aliases), and 1 MiB retained plus provisional decoded text including
terminators. Superseded strings release the byte budget; the expanded-string
count remains monotonic. Static defaults consume no owned text. These constructors
keep the legacy individual string-length policy rather than adding the ordinary
10 KiB field guard. Excess fails closed with a location diagnostic; validation/lookup decoding, parser DOM,
temporary provider decoding and allocator/container overhead are outside these
bounds. No unbounded legacy retry is used.

Each retains content-based items/matrix selection, including empty items with a
nonempty matrix, and rejects an empty dimension before later duplicates. Triggers
validate success/failure/complete in that fixed order, accept existing scalar task
names and refuse present empty groups. DynamicTasks retains all required fields,
plain scalar command errors, full-list decode before empty-entry checks and the
presence of explicitly empty timeout/when values. Uses retains plain scalar
conversion. Raw-key lookup, decoded-NUL truncation and provider/owned allocation
failure origins remain unchanged. Rollback requires restoring constructors and
wiring together; no parallel C++ algorithm remains as a fallback.

Twelve native TinyTest cases cover these lifetimes, typed views, decoded duplicate
keys/order, coercion, validation precedence, concurrent independent owners, exact
admission boundaries, and byte-budget release on replacement. Six boundary-fault
cases walk owner/list allocation, actual YAML decode/copy, CSTL vector/tree
creation/insertion and early/late diagnostic failures. Formal application tests
cover the real typed-to-STL projection and exception categories. Both new C-only
executables join runtime link audits, lifecycle ASan and complete-source TSan;
existing application YAML/cancellation/timeout regressions remain in the complete
graph. Frozen old constructors and their pre-migration YAML facade additionally
matched 45,102 complete local payload/exception/diagnostic outcomes, including
unordered-map iteration, long scalars and competing invalid fields. This is
construction parity evidence, not a claim of native DAG/trigger execution.

Command-to-Orch lowering and the orchestration node catalog now live in the
C-only `praktor_orch_tree` target. The catalog has one declaration source for all
31 types, required/optional parameter names and control/child policy. C lookup
preserves underscore-insensitive, C-locale byte case folding and length-aware
name refusal. `orch_legacy_bridge.cpp` replaces `task_types.cpp`: it projects
catalog entries using the original initializer-list construction and declaration
order. Thus the existing platform-specific unordered registry iteration, including
unknown-type diagnostics in the remaining recursive parser, stays unchanged.
The original C++ normalization/table and Command expansion algorithms are removed.

The unique C tree owns a CSTL vector of separately allocated nodes. Each node
owns parameter text in `tstr` plus raw CSTL vectors of parameters and borrowed
child pointers. Node-vector growth cannot invalidate those pointers; no owning
container is byte-copied. Iterative destruction walks the unique node vector,
never recursively destroys borrowed child pointers. Construction copies binary
`vstr` input and publishes only after complete success. Views remain valid after
input destruction and owner pointer transfer, expiring at owner destruction;
independent owners can be constructed concurrently. Failure destroys all partial
nodes, parameters and vector storage and preserves the prior caller output.

The constructor bounds command lists to 4096 entries, trees to 4098 nodes,
per-node parameters to eight and aggregate owned parameter text to 1 MiB including
terminators, generated capture-group text and default values. Static keys/type
names consume no text budget. Container and allocator overhead is outside that
text bound. Capacity excess fails closed with no truncation or Legacy retry.
These limits also apply to manual/dynamic Command lowering, which was previously
unbounded; defaults can make tree admission stricter than YAML parameter-owner
admission near the aggregate boundary. This is an explicit branch-local policy,
not an installed SDK ABI change. Only the selected scalar/list form and highest
priority parser are read. Postprocessor precedence, empty manual command/list
behavior, binary values, last-Shell stdout assignment, regex integer rendering,
empty Lines filter omission and the JSON `data` fallback remain unchanged.
Working-directory/environment fields remain outside this existing lowering policy.

A fourth independent `praktor/src/yml/standalone` project requires only the base
Salts SDK, C compiler and Core/CSTL/TinyTest/Platform targets: it does not load the
YAML/JSON, Process or application targets. Its two C executables cover catalog
lookup, stable input-independent trees, all parser choices/precedence, malformed
views, exact capacity boundaries, concurrent owners and rollback at public owner,
string, vector and generated-text allocation boundaries. The same fault provider
is linked into the existing C++ exception test, so the real conversion boundary
is exercised through every native allocation failure. A native YAML integration
case passes actual C-owned Command parameters into the C tree and then destroys
both YAML/parameter owners before inspecting the tree. Existing native Process
controller/output-scanner tests and application cancellation/timeout regressions
remain enabled. Frozen old C++ catalog and lowering code additionally matched
29,411 complete tree payloads, parameter/registry iteration order and all 31 type
metadata entries.

This cut establishes a reusable owned tree, not a second recursive parser or a
new executor. The 744-line recursive Orch YAML parser still requires migration
into this owner, and task/workflow representation and DAG execution remain C++.
The temporary bridge copies typed views only; retaining old expansion as a hidden
fallback was rejected. Rollback restores catalog/lowering algorithms and their
call wiring together. Both new C executables join lifecycle ASan and complete-
source TSan, alongside the complete application graph.

The independently migrated harness-profile policy is preserved in this cut.
Its standalone C project gains matching release/ASan presets and joins the same
full-graph build/link qualification; the existing native profile regression joins
lifecycle ASan and complete-source TSan filters. The policy remains the sole
reason-mask/ordering authority; no parallel C++ policy is reintroduced. The
reviewed-plan caller's explicit value projection still needs migration.

All five independent projects provide release/ASan presets. On Linux,
`bash cmake/ci/qualify-c-only-tools.sh release` configures/builds/tests all five
complete graphs and rejects C++ symbols/libraries in their executable links.
The complete-source TSan workflow invokes the same qualification with `thread`
against instrumented Salts and SaltsUtils. Ordinary platform CI continues
building/testing the application and installed SDK during migration.

Keep this follow-up Draft until the whole-program gates above pass. No
automatic merge or publication is authorized by the migration request. The
merged master stays on its accepted backend while this branch is incomplete.
