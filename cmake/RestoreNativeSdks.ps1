param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('windows-x64', 'linux-x64', 'macos-arm64', 'android-arm64-v8a')]
    [string]$Rid,
    [switch]$CoreOnly,
    [ValidatePattern('^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$')]
    [string]$SaltsUtilsVersion
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
    throw 'GITHUB_TOKEN with read:packages permission is required'
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$arguments = @(
    (Join-Path $PSScriptRoot 'ci/github_packages.py'), 'sdks', '--rid', $Rid,
    '--packages', (Join-Path $repositoryRoot 'build/native-sdk/packages')
)
if ($CoreOnly) { $arguments += '--core-only' }
if ($SaltsUtilsVersion) { $arguments += @('--salts-utils-version', $SaltsUtilsVersion) }
# Keep this development entry point; CI and local use share the curl downloader.
$lines = @(& python @arguments)
if ($LASTEXITCODE -ne 0) { throw 'Failed to download native SDK packages with curl' }
foreach ($line in $lines) {
    Write-Host $line
    if ($line -match '^((?:SALTS|SALTS_UTILS|CHTTP|TURBOSCRIPT)_(?:ROOT|NATIVE_VERSION))=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
