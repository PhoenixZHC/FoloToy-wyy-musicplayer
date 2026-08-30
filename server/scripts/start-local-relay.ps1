$ErrorActionPreference = 'Stop'

function New-HexString {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ByteCount
    )

    $bytes = New-Object byte[] $ByteCount
    $rng = [System.Security.Cryptography.RNGCryptoServiceProvider]::Create()
    try {
        $rng.GetBytes($bytes)
    } finally {
        $rng.Dispose()
    }
    return [System.BitConverter]::ToString($bytes).Replace('-', '').ToLowerInvariant()
}

function Read-LocalRelayEnv {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $values = @{}
    if (Test-Path -LiteralPath $Path) {
        foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
            $trimmed = $line.Trim()
            if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) {
                continue
            }
            $parts = $trimmed.Split('=', 2)
            if ($parts.Count -eq 2) {
                $values[$parts[0]] = $parts[1]
            }
        }
    }
    return $values
}

function Write-LocalRelayEnv {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [hashtable]$Values
    )

    $lines = @(
        "COOKIE_ENCRYPTION_KEY=$($Values.COOKIE_ENCRYPTION_KEY)",
        "DEVICE_REGISTRATION_TOKEN=$($Values.DEVICE_REGISTRATION_TOKEN)",
        "NETEASE_API_BASE_URL=$($Values.NETEASE_API_BASE_URL)"
    )
    Set-Content -LiteralPath $Path -Value $lines -Encoding UTF8
}

function Get-LanIpv4 {
    try {
        $configs = Get-NetIPConfiguration | Where-Object {
            $_.IPv4DefaultGateway -and $_.NetAdapter.Status -eq 'Up'
        }
        foreach ($config in $configs) {
            foreach ($address in $config.IPv4Address) {
                if ($address.IPAddress -and $address.IPAddress -notmatch '^(127|169\.254)\.') {
                    return $address.IPAddress
                }
            }
        }
    } catch {
    }

    try {
        $hostName = [System.Net.Dns]::GetHostName()
        $addresses = [System.Net.Dns]::GetHostAddresses($hostName)
        foreach ($address in $addresses) {
            if ($address.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork) {
                $ip = $address.ToString()
                if ($ip -notmatch '^(127|169\.254)\.') {
                    return $ip
                }
            }
        }
    } catch {
    }

    return '127.0.0.1'
}

function Test-NeteaseOutlet {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BaseUrl
    )

    try {
        $uri = $BaseUrl.TrimEnd('/') + '/login/qr/key'
        Invoke-WebRequest -UseBasicParsing -Uri $uri -TimeoutSec 5 -ErrorAction Stop | Out-Null
        return $true
    } catch {
        return $false
    }
}

$serverRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
Set-Location -LiteralPath $serverRoot

$dataDir = Join-Path $serverRoot 'data'
if (-not (Test-Path -LiteralPath $dataDir)) {
    New-Item -ItemType Directory -Path $dataDir | Out-Null
}

$envPath = Join-Path $dataDir 'local-relay.env'
$localEnv = Read-LocalRelayEnv -Path $envPath

if (-not $localEnv.ContainsKey('COOKIE_ENCRYPTION_KEY') -or $localEnv.COOKIE_ENCRYPTION_KEY.Length -ne 32) {
    $localEnv.COOKIE_ENCRYPTION_KEY = New-HexString -ByteCount 16
}
if (-not $localEnv.ContainsKey('DEVICE_REGISTRATION_TOKEN') -or -not $localEnv.DEVICE_REGISTRATION_TOKEN.Trim()) {
    $localEnv.DEVICE_REGISTRATION_TOKEN = 'pair-' + (New-HexString -ByteCount 8)
}
if (-not $localEnv.ContainsKey('NETEASE_API_BASE_URL') -or -not $localEnv.NETEASE_API_BASE_URL.Trim()) {
    $localEnv.NETEASE_API_BASE_URL = 'http://127.0.0.1:3000'
}

Write-LocalRelayEnv -Path $envPath -Values $localEnv

$env:PORT = if ($env:PORT) { $env:PORT } else { '18080' }
$env:DATABASE_PATH = if ($env:DATABASE_PATH) { $env:DATABASE_PATH } else { Join-Path $dataDir 'local-relay.db' }
$env:COOKIE_ENCRYPTION_KEY = if ($env:COOKIE_ENCRYPTION_KEY) { $env:COOKIE_ENCRYPTION_KEY } else { $localEnv.COOKIE_ENCRYPTION_KEY }
$env:DEVICE_REGISTRATION_TOKEN = if ($env:DEVICE_REGISTRATION_TOKEN) { $env:DEVICE_REGISTRATION_TOKEN } else { $localEnv.DEVICE_REGISTRATION_TOKEN }
$env:NETEASE_API_BASE_URL = if ($env:NETEASE_API_BASE_URL) { $env:NETEASE_API_BASE_URL } else { $localEnv.NETEASE_API_BASE_URL }
$env:LOCAL_RELAY_MODE = 'true'
$env:MOCK_NETEASE = 'false'
$env:ALLOW_STALE_DEVICE_TIMESTAMPS = if ($env:ALLOW_STALE_DEVICE_TIMESTAMPS -eq 'true') { 'true' } else { 'false' }

npm run build
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$lanIp = Get-LanIpv4
$relayUrl = "http://${lanIp}:$($env:PORT)"
$outletReady = Test-NeteaseOutlet -BaseUrl $env:NETEASE_API_BASE_URL

Write-Host ''
Write-Host 'Local relay is starting.'
Write-Host "Relay URL: $relayUrl"
Write-Host "Registration Code: $($env:DEVICE_REGISTRATION_TOKEN)"
Write-Host "NetEase API outlet: $($env:NETEASE_API_BASE_URL)"
if (-not $outletReady) {
    Write-Host 'Warning: NetEase API outlet did not respond. QR login will fail until it is started.'
}
Write-Host ''
Write-Host 'Device setup fields:'
Write-Host 'Wi-Fi SSID: your 2.4 GHz Wi-Fi name'
Write-Host 'Wi-Fi Password: your Wi-Fi password'
Write-Host "Relay URL: $relayUrl"
Write-Host "Registration Code: $($env:DEVICE_REGISTRATION_TOKEN)"
Write-Host ''

node 'dist/app.js'
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
