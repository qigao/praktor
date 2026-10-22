# TinyTest tests

Praktor tests use the installed `Salts::TinyTest` target and `tinytest.hpp`.
Catch2 is not a build or package dependency. CTest executable names are unchanged.

The migration preserves 315 original test cases as 505 independent TinyTest
executions, before later additions. Each former section path has an explicit
`it` entry. Its helper receives a section index and constructs a fresh fixture
for that invocation. Plain `std::array` data and nested `it` enumeration replace
generators, preserving Cartesian products and fresh setup for each row. Dynamic
input sections likewise have separate entries and fresh workflow fixtures.

All migrated assertions use native failing `check` operations. TinyTest's
`check_warn` is diagnostic-only, so it must never replace an assertion. Former
non-fatal checks now fail fast rather than silently becoming warnings.
TinyTest unwinds C++ destructors on fatal failure. Exception-fragment helpers
execute the operation once, verify every fragment against that exception and
rethrow TinyTest's own failure signal. Numeric comparisons retain the former
default relative tolerance. Windows-only cases use `it_skip` on other hosts.

`praktor_test.hpp` supplies only string diagnostics and the multi-fragment
exception predicate; registration, assertions, filtering and the runner all
come from TinyTest. Prefer descriptive `spec`/`it` entries for new tests.
