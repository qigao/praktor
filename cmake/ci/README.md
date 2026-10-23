Native CI restores GitHub packages with `curl`, Python's standard library and
NuGet v3 download metadata. It does not install or run Mono/NuGet. The common
`setup-native-ci` action bootstraps the immutable shared vcpkg contract and
restores the re2c host executable; Core-only, Native SDK and source TSan use it.

`github_packages.py sdks --rid linux-x64 --core-only --packages build/native-sdk/packages`
restores the published SDKs. SaltsUtils remains pinned to `4.3.0-rc.11` unless
explicitly overridden; Salts/CHttp include prereleases, while TurboScript and
re2c select stable versions, matching the previous restore policy. SDK roots
are exported only after every requested package and CMake config is validated.
The PowerShell development entry point calls the same downloader.

The read-only loopback HTTP adapter lets vcpkg consume the existing GitHub NuGet
binary cache using its native HTTP provider. It selects the exact ABI suffix,
verifies the SHA-256 of `vcpkg_abi_info.txt`, removes NuGet metadata and writes a
plain zip to the local ABI cache. A cache miss falls back to the normal vcpkg
build. The adapter uses the job token from its environment and the job's process
cleanup ends it. Credentials are passed to curl on stdin, never in command-line
arguments or generated NuGet config files. Its log is uploaded with diagnostics.
The upstream tool/scripts versions, overlay content and canonical overlay paths
are preserved. No cache package is uploaded by these jobs.

`python cmake/ci/github_packages_test.py` uses actual curl against an authenticated
local feed to cover version selection, paginated metadata, package identity/path
validation, atomic SDK environment publication and exact ABI cache conversion.

The Native SDK pack job uses only tested artifacts and an empty package source
list (`RestoreNativeDependencies=False`). .NET remains confined to packing and
tag-gated publication. PR #78 remains Draft; no merge or publication is enabled.
