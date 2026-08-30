param(
    [switch]$RunNeteaseOutlet
)

$ErrorActionPreference = 'Stop'

function Test-NeteaseOutlet {
    try {
        Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:3000/login/qr/key' -TimeoutSec 3 -ErrorAction Stop | Out-Null
        return $true
    } catch {
        return $false
    }
}

function Test-PortInUse {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Port
    )

    return [bool](Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)
}

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

if ($RunNeteaseOutlet) {
    $npx = Get-Command 'npx.cmd' -ErrorAction Stop
    & $npx.Source --yes 'NeteaseCloudMusicApi@4.32.0'
    exit $LASTEXITCODE
}

$serverRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
Set-Location -LiteralPath $serverRoot

if (Test-PortInUse -Port 18080) {
    try {
        $health = Invoke-RestMethod -Uri 'http://127.0.0.1:18080/health' -TimeoutSec 3 -ErrorAction Stop
        if ($health.ok -eq $true) {
            Write-Host 'Local relay is already running on port 18080.'
            exit 0
        }
    } catch {
    }

    throw 'Port 18080 is already in use by another program.'
}

$outletProcess = $null
$dataDir = Join-Path $serverRoot 'data'
if (-not (Test-Path -LiteralPath $dataDir)) {
    New-Item -ItemType Directory -Path $dataDir | Out-Null
}

try {
    if (-not (Test-NeteaseOutlet)) {
        if (Test-PortInUse -Port 3000) {
            throw 'Port 3000 is occupied, but the NetEase API outlet did not respond correctly.'
        }

        $powershellPath = [System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
        $outletOutput = Join-Path $dataDir 'netease-api.out.log'
        $outletError = Join-Path $dataDir 'netease-api.err.log'
        $outletArguments = @(
            '-NoProfile',
            '-ExecutionPolicy',
            'Bypass',
            '-File',
            "`"$PSCommandPath`"",
            '-RunNeteaseOutlet'
        )

        Write-Host 'Starting NetEase API outlet...'
        $outletProcess = Start-Process -FilePath $powershellPath -ArgumentList $outletArguments -PassThru -NoNewWindow -RedirectStandardOutput $outletOutput -RedirectStandardError $outletError

        $deadline = [DateTime]::UtcNow.AddSeconds(60)
        while (-not (Test-NeteaseOutlet)) {
            if ($outletProcess.HasExited) {
                throw "NetEase API outlet exited early. Check: $outletError"
            }
            if ([DateTime]::UtcNow -ge $deadline) {
                throw "NetEase API outlet did not start within 60 seconds. Check: $outletError"
            }
            Start-Sleep -Milliseconds 500
        }

        Write-Host 'NetEase API outlet is ready on port 3000.'
    } else {
        Write-Host 'Using the existing NetEase API outlet on port 3000.'
    }

    Write-Host 'Starting local relay...'
    Write-Host 'Keep this window open. Press Ctrl+C to stop.'
    Write-Host ''

    & (Join-Path $PSScriptRoot 'start-local-relay.ps1')
} finally {
    if ($outletProcess -and -not $outletProcess.HasExited) {
        Write-Host ''
        Write-Host 'Stopping NetEase API outlet started by this launcher...'
        Stop-ProcessTree -ProcessId $outletProcess.Id
    }
}
