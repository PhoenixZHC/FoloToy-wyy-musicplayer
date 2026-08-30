param(
    [switch]$SelfTest,
    [ValidateRange(1024, 65535)]
    [int]$RelayPort = 18080,
    [ValidateRange(0, 65535)]
    [int]$OutletPort = 0
)

$ErrorActionPreference = 'Stop'

function New-HexString {
    param([Parameter(Mandatory = $true)][int]$ByteCount)

    $bytes = New-Object byte[] $ByteCount
    $rng = [System.Security.Cryptography.RNGCryptoServiceProvider]::Create()
    try {
        $rng.GetBytes($bytes)
    } finally {
        $rng.Dispose()
    }
    return [System.BitConverter]::ToString($bytes).Replace('-', '').ToLowerInvariant()
}

function Test-PortInUse {
    param([Parameter(Mandatory = $true)][int]$Port)

    return [bool](Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)
}

function Get-AvailableTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally {
        $listener.Stop()
    }
}

function Wait-HttpReady {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$ServiceName,
        [Parameter(Mandatory = $true)][string]$ErrorLog
    )

    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Process.HasExited) {
            throw "$ServiceName 启动后立即退出，请查看：$ErrorLog"
        }
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $Url -TimeoutSec 3 -ErrorAction Stop | Out-Null
            return
        } catch {
            Start-Sleep -Milliseconds 400
        }
    }
    throw "$ServiceName 在 60 秒内没有就绪，请查看：$ErrorLog"
}

function Stop-ProcessTree {
    param([Parameter(Mandatory = $true)][int]$ProcessId)

    $children = Get-CimInstance Win32_Process -Filter "ParentProcessId=$ProcessId" -ErrorAction SilentlyContinue
    foreach ($child in $children) {
        Stop-ProcessTree -ProcessId $child.ProcessId
    }
    Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
}

function Clear-ListeningPort {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$ServiceName
    )

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline) {
        $listeners = @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)
        if ($listeners.Count -eq 0) {
            return
        }

        $processIds = @($listeners | Select-Object -ExpandProperty OwningProcess | Sort-Object -Unique)
        foreach ($processId in $processIds) {
            if ($processId -eq $PID) {
                throw "$ServiceName 端口 $Port 被当前启动脚本占用，无法自动释放。"
            }

            $process = Get-Process -Id $processId -ErrorAction SilentlyContinue
            if ($process -eq $null) {
                continue
            }
            Write-Host "端口 $Port 被 $($process.ProcessName) 占用，正在强制结束 PID $processId..." -ForegroundColor Yellow
            Stop-Process -Id $processId -Force -ErrorAction Stop
        }
        Start-Sleep -Milliseconds 200
    }

    $remaining = @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)
    if ($remaining.Count -gt 0) {
        $remainingIds = ($remaining | Select-Object -ExpandProperty OwningProcess | Sort-Object -Unique) -join ', '
        throw "$ServiceName 端口 $Port 在强制结束进程后仍被占用，PID：$remainingIds"
    }
}

function Get-LanIpv4 {
    $configs = Get-NetIPConfiguration -ErrorAction SilentlyContinue | Where-Object {
        $_.IPv4DefaultGateway -and $_.NetAdapter.Status -eq 'Up'
    }
    foreach ($config in $configs) {
        foreach ($address in $config.IPv4Address) {
            if ($address.IPAddress -and $address.IPAddress -notmatch '^(127|169\.254)\.') {
                return $address.IPAddress
            }
        }
    }
    return '127.0.0.1'
}

function Read-RelayEnv {
    param([Parameter(Mandatory = $true)][string]$Path)

    $values = @{}
    if (Test-Path -LiteralPath $Path) {
        foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
            $parts = $line.Trim().Split('=', 2)
            if ($parts.Count -eq 2 -and $parts[0]) {
                $values[$parts[0]] = $parts[1]
            }
        }
    }
    return $values
}

function Write-RelayEnv {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][hashtable]$Values
    )

    $lines = @(
        "COOKIE_ENCRYPTION_KEY=$($Values.COOKIE_ENCRYPTION_KEY)",
        "DEVICE_REGISTRATION_TOKEN=$($Values.DEVICE_REGISTRATION_TOKEN)"
    )
    Set-Content -LiteralPath $Path -Value $lines -Encoding UTF8
}

$packageRoot = Resolve-Path -LiteralPath $PSScriptRoot
$nodePath = Join-Path $packageRoot 'runtime\node.exe'
$relayEntry = Join-Path $packageRoot 'app\dist\app.js'
$outletRoot = Join-Path $packageRoot 'outlet\node_modules\NeteaseCloudMusicApi'
$outletEntry = Join-Path $packageRoot 'outlet\outlet-entry.js'
$dataDir = if ($SelfTest) {
    Join-Path ([System.IO.Path]::GetTempPath()) ('FoloToy-Relay-SelfTest-' + [Guid]::NewGuid().ToString('N'))
} else {
    Join-Path $packageRoot 'data'
}

foreach ($requiredPath in @($nodePath, $relayEntry, $outletEntry)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "发布包不完整，缺少：$requiredPath"
    }
}
if ($OutletPort -eq 0) {
    $OutletPort = Get-AvailableTcpPort
}
if (Test-PortInUse -Port $RelayPort) {
    Clear-ListeningPort -Port $RelayPort -ServiceName '中转服务'
}
if (Test-PortInUse -Port $OutletPort) {
    Clear-ListeningPort -Port $OutletPort -ServiceName '网易云接口'
}
if (-not (Test-Path -LiteralPath $dataDir)) {
    New-Item -ItemType Directory -Path $dataDir | Out-Null
}

$envPath = Join-Path $dataDir 'local-relay.env'
$localEnv = Read-RelayEnv -Path $envPath
if (-not $localEnv.ContainsKey('COOKIE_ENCRYPTION_KEY') -or $localEnv.COOKIE_ENCRYPTION_KEY.Length -ne 32) {
    $localEnv.COOKIE_ENCRYPTION_KEY = New-HexString -ByteCount 16
}
if (-not $localEnv.ContainsKey('DEVICE_REGISTRATION_TOKEN') -or -not $localEnv.DEVICE_REGISTRATION_TOKEN.Trim()) {
    $localEnv.DEVICE_REGISTRATION_TOKEN = 'pair-' + (New-HexString -ByteCount 8)
}
Write-RelayEnv -Path $envPath -Values $localEnv

$outletProcess = $null
$relayProcess = $null
$outletOut = Join-Path $dataDir 'netease-api.out.log'
$outletErr = Join-Path $dataDir 'netease-api.err.log'
$relayOut = Join-Path $dataDir 'relay.out.log'
$relayErr = Join-Path $dataDir 'relay.err.log'

try {
    $env:PORT = [string]$OutletPort
    $env:HOST = '127.0.0.1'
    $env:NODE_ENV = 'production'
    $outletProcess = Start-Process -FilePath $nodePath -ArgumentList @("`"$outletEntry`"") -WorkingDirectory $outletRoot -PassThru -WindowStyle Hidden -RedirectStandardOutput $outletOut -RedirectStandardError $outletErr
    Wait-HttpReady -Url "http://127.0.0.1:$OutletPort/" -Process $outletProcess -ServiceName '网易云接口' -ErrorLog $outletErr
    try {
        Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$OutletPort/cloud" -TimeoutSec 3 -ErrorAction Stop | Out-Null
        throw '未使用的网易云云盘接口仍然可以访问。'
    } catch {
        $statusCode = if ($_.Exception.Response) { [int]$_.Exception.Response.StatusCode } else { 0 }
        if ($statusCode -ne 404) {
            throw
        }
    }
    $outletListeners = @(Get-NetTCPConnection -State Listen -LocalPort $OutletPort -ErrorAction Stop)
    $publicOutletListeners = @($outletListeners | Where-Object { $_.LocalAddress -notin @('127.0.0.1', '::1') })
    if ($outletListeners.Count -eq 0 -or $publicOutletListeners.Count -gt 0) {
        throw '网易云接口没有限制在本机访问，已停止启动。'
    }

    $env:PORT = [string]$RelayPort
    $env:DATABASE_PATH = Join-Path $dataDir 'local-relay.db'
    $env:COOKIE_ENCRYPTION_KEY = $localEnv.COOKIE_ENCRYPTION_KEY
    $env:DEVICE_REGISTRATION_TOKEN = $localEnv.DEVICE_REGISTRATION_TOKEN
    $env:NETEASE_API_BASE_URL = "http://127.0.0.1:$OutletPort"
    $env:LOCAL_RELAY_MODE = 'true'
    $env:MOCK_NETEASE = 'false'
    $env:ALLOW_STALE_DEVICE_TIMESTAMPS = 'false'
    $relayProcess = Start-Process -FilePath $nodePath -ArgumentList @("`"$relayEntry`"") -WorkingDirectory (Join-Path $packageRoot 'app') -PassThru -WindowStyle Hidden -RedirectStandardOutput $relayOut -RedirectStandardError $relayErr
    Wait-HttpReady -Url "http://127.0.0.1:$RelayPort/health" -Process $relayProcess -ServiceName '中转服务' -ErrorLog $relayErr

    if ($SelfTest) {
        Write-Output 'OUTLET_BINDING=LOCALHOST'
        Write-Output 'OUTLET_ROUTE_SCOPE=MINIMAL'
        Write-Output 'PORTABLE_RELAY_SELF_TEST=PASS'
        exit 0
    }

    $lanIp = Get-LanIpv4
    Write-Host ''
    Write-Host '中转服务已启动。' -ForegroundColor Green
    Write-Host "Relay URL: http://${lanIp}:$RelayPort" -ForegroundColor Cyan
    Write-Host "Registration Code: $($localEnv.DEVICE_REGISTRATION_TOKEN)" -ForegroundColor Cyan
    Write-Host ''
    Write-Host '请保持此窗口打开；关闭窗口或按 Ctrl+C 即可停止服务。'
    Write-Host '如果 Windows 弹出防火墙提示，请允许在专用网络中访问。'
    Wait-Process -Id $relayProcess.Id
} finally {
    if ($relayProcess -and -not $relayProcess.HasExited) {
        Stop-ProcessTree -ProcessId $relayProcess.Id
    }
    if ($outletProcess -and -not $outletProcess.HasExited) {
        Stop-ProcessTree -ProcessId $outletProcess.Id
    }
}
