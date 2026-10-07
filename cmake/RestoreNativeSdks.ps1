param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('windows-x64', 'linux-x64', 'macos-arm64', 'android-arm64-v8a')]
    [string]$Rid,
    [switch]$CoreOnly
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
    throw 'GITHUB_TOKEN with read:packages permission is required'
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$packages = Join-Path $repositoryRoot 'build/native-sdk/packages'
$intermediate = Join-Path $repositoryRoot 'build/native-sdk/obj/'
$project = Join-Path $repositoryRoot 'packaging/nuget/Praktor.Native.csproj'
dotnet restore $project --packages $packages `
    --configfile "$repositoryRoot/cmake/vcpkg-cache.nuget.config" --no-cache --force-evaluate `
    "-p:BaseIntermediateOutputPath=$intermediate" "-p:EnableScriptEngine=$(!$CoreOnly)"
if ($LASTEXITCODE -ne 0) { throw 'Failed to restore the latest native SDKs' }

$assets = Get-Content -LiteralPath "${intermediate}project.assets.json" -Raw |
    ConvertFrom-Json -AsHashtable
$dependencies = [ordered]@{
    SALTS_ROOT = @('Salts.Native', 'Salts')
    SALTS_UTILS_ROOT = @('SaltsUtils.Native', 'SaltsUtils')
    CHTTP_ROOT = @('CHttp.Native', 'Chttp')
}
if (-not $CoreOnly) { $dependencies.TURBOSCRIPT_ROOT = @('TurboScript.Native', 'TurboScript') }
$roots = [ordered]@{}
foreach ($name in $dependencies.Keys) {
    $packageId, $cmakePackage = $dependencies[$name]
    $keys = @($assets.libraries.Keys | Where-Object {
        $_.StartsWith("$packageId/", [StringComparison]::OrdinalIgnoreCase)
    })
    if ($keys.Count -ne 1) { throw "Expected one resolved $packageId package" }
    Write-Host "Resolved $($keys[0]) for $Rid"
    $root = Join-Path (Join-Path $packages $assets.libraries[$keys[0]].path) "sdk/$Rid"
    if (-not (Test-Path -LiteralPath "$root/lib/cmake/$cmakePackage/${cmakePackage}Config.cmake" -PathType Leaf)) {
        throw "$packageId does not provide a $Rid SDK"
    }
    $roots[$name] = $root
}

# Publish only a complete set; stale cached package directories are never selected.
foreach ($name in $roots.Keys) {
    [Environment]::SetEnvironmentVariable($name, $roots[$name], 'Process')
    if ($env:GITHUB_ACTIONS -eq 'true') {
        "$name=$($roots[$name])" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
    }
}
