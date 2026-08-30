param(
    [string]$PackageName = 'FoloToy-Wangyiyun-Relay-Windows-x64'
)

$ErrorActionPreference = 'Stop'
$serverRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
$workspaceRoot = Resolve-Path -LiteralPath (Join-Path $serverRoot '..')
$releaseRoot = Join-Path $workspaceRoot 'release'
$packageRoot = Join-Path $releaseRoot $PackageName
$zipPath = Join-Path $releaseRoot ($PackageName + '.zip')
$nodeVersion = '24.20.0'
$nodeArchiveName = "node-v$nodeVersion-win-x64.zip"
$nodeArchiveSha256 = '6cac9ffbca8f6a47091e4b5c772e0606049c3871cb67d900c0cedde630e545ba'
$nodeDownloadUrl = "https://nodejs.org/dist/v$nodeVersion/$nodeArchiveName"
$cacheRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'FoloToy-Release-Cache'
$nodeArchive = Join-Path $cacheRoot $nodeArchiveName
$nodeDistribution = Join-Path $cacheRoot "node-v$nodeVersion-win-x64"

if ($PackageName -notmatch '^[A-Za-z0-9._-]+$') {
    throw 'PackageName contains unsupported characters.'
}
if (-not (Test-Path -LiteralPath $releaseRoot)) {
    New-Item -ItemType Directory -Path $releaseRoot | Out-Null
}

$resolvedReleaseRoot = (Resolve-Path -LiteralPath $releaseRoot).Path
foreach ($target in @($packageRoot, $zipPath)) {
    $targetParent = [System.IO.Path]::GetFullPath((Split-Path -Parent $target))
    if ($targetParent -ne $resolvedReleaseRoot) {
        throw "Refusing to replace a path outside the release directory: $target"
    }
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
}

if (-not (Test-Path -LiteralPath $cacheRoot)) {
    New-Item -ItemType Directory -Path $cacheRoot | Out-Null
}
$downloadRequired = -not (Test-Path -LiteralPath $nodeArchive -PathType Leaf)
if (-not $downloadRequired) {
    $cachedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $nodeArchive).Hash.ToLowerInvariant()
    $downloadRequired = $cachedHash -ne $nodeArchiveSha256
}
if ($downloadRequired) {
    $downloadPath = $nodeArchive + '.download'
    curl.exe -fL --retry 3 --output $downloadPath $nodeDownloadUrl
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $downloadHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $downloadPath).Hash.ToLowerInvariant()
    if ($downloadHash -ne $nodeArchiveSha256) {
        throw "Node.js archive checksum mismatch. Expected $nodeArchiveSha256, got $downloadHash"
    }
    Move-Item -LiteralPath $downloadPath -Destination $nodeArchive -Force
}
if (-not (Test-Path -LiteralPath (Join-Path $nodeDistribution 'node.exe') -PathType Leaf)) {
    Expand-Archive -LiteralPath $nodeArchive -DestinationPath $cacheRoot
}
$cachedNode = Join-Path $nodeDistribution 'node.exe'
$nodeInfo = & $cachedNode -p "JSON.stringify({version:process.versions.node,arch:process.arch,platform:process.platform})" | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $nodeInfo.version -ne $nodeVersion -or $nodeInfo.arch -ne 'x64' -or $nodeInfo.platform -ne 'win32') {
    throw "Unexpected cached Node.js runtime: $($nodeInfo.version) $($nodeInfo.arch) $($nodeInfo.platform)"
}

Set-Location -LiteralPath $serverRoot
npm run build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$appRoot = Join-Path $packageRoot 'app'
$outletRoot = Join-Path $packageRoot 'outlet'
$runtimeRoot = Join-Path $packageRoot 'runtime'
New-Item -ItemType Directory -Path $appRoot, $outletRoot, $runtimeRoot | Out-Null

Copy-Item -LiteralPath (Join-Path $serverRoot 'portable\启动中转服务.cmd') -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $serverRoot 'portable\运行中转服务.ps1') -Destination (Join-Path $packageRoot 'run-relay.ps1')
Copy-Item -LiteralPath (Join-Path $serverRoot 'portable\使用说明.txt') -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $serverRoot 'portable\outlet-entry.js') -Destination $outletRoot
Copy-Item -Path (Join-Path $nodeDistribution '*') -Destination $runtimeRoot -Recurse
Copy-Item -LiteralPath (Join-Path $serverRoot 'package.json') -Destination $appRoot
Copy-Item -LiteralPath (Join-Path $serverRoot 'package-lock.json') -Destination $appRoot
Copy-Item -LiteralPath (Join-Path $serverRoot 'dist') -Destination $appRoot -Recurse
Copy-Item -LiteralPath (Join-Path $serverRoot 'migrations') -Destination $appRoot -Recurse

Set-Location -LiteralPath $appRoot
$bundledNpm = Join-Path $runtimeRoot 'npm.cmd'
$env:Path = $runtimeRoot + ';' + $env:Path
& $bundledNpm ci --omit=dev
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $bundledNpm install --omit=dev --save-exact better-sqlite3@13.0.3
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $runtimeRoot 'node.exe') -e "const Database=require('./node_modules/better-sqlite3'); const db=new Database(':memory:'); db.close()"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$outletPackageJson = @'
{
  "name": "folotoy-netease-outlet-portable",
  "private": true,
  "version": "1.0.0",
  "dependencies": {
    "NeteaseCloudMusicApi": "4.32.0"
  },
  "overrides": {
    "music-metadata": "11.15.0",
    "file-type": "22.0.2"
  }
}
'@
Set-Content -LiteralPath (Join-Path $outletRoot 'package.json') -Value $outletPackageJson -Encoding UTF8
Set-Location -LiteralPath $outletRoot
& $bundledNpm install --omit=dev --no-audit --no-fund
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Set-Location -LiteralPath $workspaceRoot
Compress-Archive -LiteralPath $packageRoot -DestinationPath $zipPath -CompressionLevel Optimal
if (-not (Test-Path -LiteralPath $zipPath -PathType Leaf)) {
    throw 'Portable release ZIP was not created.'
}

Write-Output "PORTABLE_PACKAGE_ROOT=$packageRoot"
Write-Output "PORTABLE_PACKAGE_ZIP=$zipPath"
