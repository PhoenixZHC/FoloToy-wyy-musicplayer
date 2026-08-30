$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$OfficialRoot = Split-Path -Parent (Split-Path -Parent $ProjectDir)
$ExpectedComponentRoot = Join-Path $OfficialRoot 'firmware\components'
$CMakePath = Join-Path $ProjectDir 'CMakeLists.txt'
$MainRoot = Join-Path $ProjectDir 'main'
$OfficialClientRoot = Join-Path $OfficialRoot 'firmware\components\netease_official_client'
$TestsRoot = Join-Path $ProjectDir 'tests'

if (-not (Test-Path -LiteralPath $CMakePath)) {
    throw "Missing project CMake file: $CMakePath"
}

$cmake = Get-Content -LiteralPath $CMakePath -Encoding UTF8 -Raw
if ($cmake -notmatch 'set\s*\(\s*EXTRA_COMPONENT_DIRS\s+"../components"\s*\)') {
    throw 'EXTRA_COMPONENT_DIRS must remain "../components"'
}

$resolvedComponentRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir '..\components'))
$expectedFull = [System.IO.Path]::GetFullPath($ExpectedComponentRoot)
if ($resolvedComponentRoot.TrimEnd('\') -ne $expectedFull.TrimEnd('\')) {
    throw "EXTRA_COMPONENT_DIRS resolves outside the official firmware tree: $resolvedComponentRoot"
}

if ($resolvedComponentRoot -notlike (Join-Path $OfficialRoot 'firmware') + '*') {
    throw "Component path escaped official firmware root: $resolvedComponentRoot"
}

$forbidden = 'netease_client|NETEASE_CLIENT|cloud_url|registration_code|device_secret|register_device'
$sourceRoots = @($MainRoot, $OfficialClientRoot, $TestsRoot)
foreach ($root in $sourceRoots) {
    if (-not (Test-Path -LiteralPath $root)) {
        throw "Missing source root: $root"
    }
    $files = Get-ChildItem -LiteralPath $root -Recurse -File -Include '*.c','*.h','*.ps1','*.md'
    foreach ($file in $files) {
        $text = Get-Content -LiteralPath $file.FullName -Encoding UTF8 -Raw
        if ($text -match $forbidden) {
            throw "Forbidden relay reference found in official source: $($file.FullName)"
        }
    }
}

$appBin = Join-Path $ProjectDir 'build\FoloToy-NetEase-Official.bin'
$bootBin = Join-Path $ProjectDir 'build\bootloader\bootloader.bin'
$partitionBin = Join-Path $ProjectDir 'build\partition_table\partition-table.bin'
$mergedBin = Join-Path $ProjectDir 'build\merged-binary.bin'
foreach ($artifact in @($appBin, $bootBin, $partitionBin, $mergedBin)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Missing build artifact: $artifact"
    }
}

Write-Output 'Official firmware validation: PASS'
