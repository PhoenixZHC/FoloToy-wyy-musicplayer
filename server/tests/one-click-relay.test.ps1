$ErrorActionPreference = 'Stop'

function Stop-ProcessTree {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ProcessId
    )

    $children = Get-CimInstance Win32_Process -Filter "ParentProcessId=$ProcessId" -ErrorAction SilentlyContinue
    foreach ($child in $children) {
        Stop-ProcessTree -ProcessId $child.ProcessId
    }

    Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
}

$serverRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
$projectRoot = Resolve-Path -LiteralPath (Join-Path $serverRoot '..')
$launcherPath = Join-Path $projectRoot '一键启动中转服务.cmd'

if (-not (Test-Path -LiteralPath $launcherPath)) {
    throw "Missing one-click launcher: $launcherPath"
}

try {
    Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:3000/login/qr/key' -TimeoutSec 5 | Out-Null
} catch {
    throw 'Test prerequisite failed: NetEase API outlet is not running on port 3000.'
}

if (Get-NetTCPConnection -State Listen -LocalPort 18080 -ErrorAction SilentlyContinue) {
    throw 'Test prerequisite failed: port 18080 is already in use.'
}

$launcher = $null
$relayHostPid = $null
try {
    $launcher = Start-Process -FilePath 'cmd.exe' -ArgumentList @('/d', '/c', "`"$launcherPath`"") -PassThru -WindowStyle Hidden
    if (-not $launcher.WaitForExit(5000)) {
        throw 'The batch launcher kept running instead of handing control to a standalone PowerShell window.'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    $healthy = $false

    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $response = Invoke-RestMethod -Uri 'http://127.0.0.1:18080/health' -TimeoutSec 2
            if ($response.ok -eq $true) {
                $healthy = $true
                break
            }
        } catch {
        }

        Start-Sleep -Milliseconds 250
    }

    if (-not $healthy) {
        throw 'One-click launcher did not make the relay healthy within 60 seconds.'
    }

    $relayListener = Get-NetTCPConnection -State Listen -LocalPort 18080 -ErrorAction Stop | Select-Object -First 1
    $ancestor = Get-CimInstance Win32_Process -Filter "ProcessId=$($relayListener.OwningProcess)"
    $nestedNpmLauncher = $false
    while ($ancestor) {
        if ($ancestor.CommandLine -match 'npm-cli\.js.+run.+start:local-relay') {
            $nestedNpmLauncher = $true
        }
        if ($ancestor.CommandLine -match 'start-all-local-relay\.ps1') {
            $relayHostPid = $ancestor.ProcessId
            break
        }
        $ancestor = Get-CimInstance Win32_Process -Filter "ProcessId=$($ancestor.ParentProcessId)" -ErrorAction SilentlyContinue
    }

    if (-not $relayHostPid) {
        throw 'Could not find the PowerShell process that owns the relay.'
    }
    if ($nestedNpmLauncher) {
        throw 'The relay is still wrapped in an npm batch process, which causes a Ctrl+C confirmation prompt.'
    }

    Write-Output 'PASS: one-click launcher started a healthy relay.'
} finally {
    if ($relayHostPid) {
        Stop-ProcessTree -ProcessId $relayHostPid
    }
    if ($launcher -and -not $launcher.HasExited) {
        Stop-ProcessTree -ProcessId $launcher.Id
    }
}
