param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = Resolve-Path -LiteralPath $PackageRoot -ErrorAction Stop

$requiredPaths = @(
    '启动中转服务.cmd',
    'run-relay.ps1',
    '使用说明.txt',
    'runtime\node.exe',
    'runtime\LICENSE',
    'app\dist\app.js',
    'app\migrations\001_initial.sql',
    'app\node_modules\better-sqlite3\package.json',
    'outlet\outlet-entry.js',
    'outlet\node_modules\NeteaseCloudMusicApi\app.js'
)

foreach ($relativePath in $requiredPaths) {
    $path = Join-Path $resolvedRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Portable release is missing: $relativePath"
    }
}

$forbiddenPaths = @(
    'data\local-relay.db',
    'data\local-relay.env',
    'data\app.db'
)
foreach ($relativePath in $forbiddenPaths) {
    if (Test-Path -LiteralPath (Join-Path $resolvedRoot $relativePath)) {
        throw "Portable release contains private data: $relativePath"
    }
}

$bundledNode = Join-Path $resolvedRoot 'runtime\node.exe'
$nodeVersion = & $bundledNode -p "process.version + ' ' + process.arch + ' ' + process.platform"
if ($LASTEXITCODE -ne 0 -or $nodeVersion -notmatch '^v24\..* x64 win32$') {
    throw "Unexpected bundled Node.js runtime: $nodeVersion"
}

$sqlitePackageJson = Join-Path $resolvedRoot 'app\node_modules\better-sqlite3\package.json'
$sqliteVersion = & $bundledNode -p "require(process.argv[1]).version" $sqlitePackageJson
if ($LASTEXITCODE -ne 0 -or $sqliteVersion -ne '13.0.3') {
    throw "Unexpected portable better-sqlite3 version: $sqliteVersion"
}

$launcher = Join-Path $resolvedRoot 'run-relay.ps1'
$windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$selfTestOutput = @(& $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $launcher -SelfTest -RelayPort 18180 -OutletPort 13000)
if ($LASTEXITCODE -ne 0) {
    throw "Portable relay self-test failed with exit code $LASTEXITCODE"
}
if ($selfTestOutput -notcontains 'OUTLET_BINDING=LOCALHOST') {
    throw 'Portable relay did not prove that the NetEase outlet is restricted to localhost.'
}
if ($selfTestOutput -notcontains 'OUTLET_ROUTE_SCOPE=MINIMAL') {
    throw 'Portable relay did not prove that unused NetEase API routes are disabled.'
}
$selfTestOutput | Write-Output

$defaultOutletBlocker = $null
try {
    $defaultOutletInUse = [bool](Get-NetTCPConnection -State Listen -LocalPort 3000 -ErrorAction SilentlyContinue)
    if (-not $defaultOutletInUse) {
        $defaultOutletBlocker = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 3000)
        $defaultOutletBlocker.Start()
    }

$automaticOutletOutput = @(& $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $launcher -SelfTest -RelayPort 18181)
    if ($LASTEXITCODE -ne 0) {
        throw "Portable relay did not select a free internal outlet port; exit code $LASTEXITCODE"
    }
    if ($automaticOutletOutput -notcontains 'PORTABLE_RELAY_SELF_TEST=PASS') {
        throw 'Portable relay automatic outlet-port self-test did not complete.'
    }
} finally {
    if ($defaultOutletBlocker) {
        $defaultOutletBlocker.Stop()
    }
}

$cmdLauncher = Join-Path $resolvedRoot '启动中转服务.cmd'
$cmdOutput = @('' | & $env:ComSpec /d /c "`"$cmdLauncher`" -SelfTest -RelayPort 18182 -OutletPort 13002" 2>&1)
if ($LASTEXITCODE -ne 0 -or $cmdOutput -notcontains 'PORTABLE_RELAY_SELF_TEST=PASS') {
    throw "Portable CMD launcher failed under cmd.exe: $($cmdOutput -join [Environment]::NewLine)"
}

$relayPortBlocker = $null
try {
    $blockerScript = "require('node:net').createServer(()=>{}).listen(18183,'127.0.0.1');setInterval(()=>{},1000)"
    $relayPortBlocker = Start-Process -FilePath $bundledNode -ArgumentList @('-e', $blockerScript) -PassThru -WindowStyle Hidden
    $blockerDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Get-NetTCPConnection -State Listen -LocalPort 18183 -ErrorAction SilentlyContinue)) {
        if ($relayPortBlocker.HasExited) {
            throw 'Relay-port blocker exited before opening its test port.'
        }
        if ([DateTime]::UtcNow -ge $blockerDeadline) {
            throw 'Relay-port blocker did not open its test port in time.'
        }
        Start-Sleep -Milliseconds 100
    }

    $forcedReleaseOutput = @(& $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $launcher -SelfTest -RelayPort 18183 -OutletPort 13003)
    if ($LASTEXITCODE -ne 0 -or $forcedReleaseOutput -notcontains 'PORTABLE_RELAY_SELF_TEST=PASS') {
        throw "Portable relay did not release an occupied relay port; exit code $LASTEXITCODE"
    }
    $relayPortBlocker.Refresh()
    if (-not $relayPortBlocker.HasExited) {
        throw 'Portable relay left the process occupying its relay port running.'
    }
} finally {
    if ($relayPortBlocker -and -not $relayPortBlocker.HasExited) {
        Stop-Process -Id $relayPortBlocker.Id -Force -ErrorAction SilentlyContinue
    }
}

foreach ($relativePath in $forbiddenPaths) {
    if (Test-Path -LiteralPath (Join-Path $resolvedRoot $relativePath)) {
        throw "Portable self-test left private data in the release: $relativePath"
    }
}

Write-Output 'PORTABLE_RELEASE_TEST=PASS'
