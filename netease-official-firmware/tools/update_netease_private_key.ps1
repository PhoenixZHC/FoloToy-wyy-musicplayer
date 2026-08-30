Param(
    [Parameter(Mandatory = $true)]
    [string]$PrivateKeyPath,

    [string]$SecretsPath = ""
)

$ErrorActionPreference = "Stop"

$rootDir = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SecretsPath)) {
    $SecretsPath = Join-Path $rootDir "config\netease_secrets.h"
}

$resolvedKey = Resolve-Path -LiteralPath $PrivateKeyPath
$resolvedSecrets = Resolve-Path -LiteralPath $SecretsPath

$rawKeyLines = Get-Content -LiteralPath $resolvedKey.Path -Encoding UTF8
$keyLines = @()
foreach ($line in $rawKeyLines) {
    $trimmed = $line.Trim()
    if ($trimmed.Length -gt 0) {
        $keyLines += $trimmed
    }
}

if ($keyLines.Count -lt 3) {
    throw "PrivateKey file is too short. Please paste the complete key."
}

if ($keyLines[0] -notmatch '^-----BEGIN (RSA )?PRIVATE KEY-----$') {
    throw "PrivateKey first line must be -----BEGIN PRIVATE KEY----- or -----BEGIN RSA PRIVATE KEY-----."
}

if ($keyLines[$keyLines.Count - 1] -notmatch '^-----END (RSA )?PRIVATE KEY-----$') {
    throw "PrivateKey last line must be -----END PRIVATE KEY----- or -----END RSA PRIVATE KEY-----."
}

$secretLines = Get-Content -LiteralPath $resolvedSecrets.Path -Encoding UTF8
$start = -1
for ($i = 0; $i -lt $secretLines.Count; $i++) {
    if ($secretLines[$i] -match '^\s*#define\s+NETEASE_OFFICIAL_PRIVATE_KEY_PEM\b') {
        $start = $i
        break
    }
}

if ($start -lt 0) {
    throw "NETEASE_OFFICIAL_PRIVATE_KEY_PEM was not found in netease_secrets.h."
}

$end = $start
while ($end -lt $secretLines.Count) {
    $current = $secretLines[$end].TrimEnd()
    if (-not $current.EndsWith('\')) {
        break
    }
    $end++
}

$newMacro = @()
$newMacro += '#define NETEASE_OFFICIAL_PRIVATE_KEY_PEM \'
for ($i = 0; $i -lt $keyLines.Count; $i++) {
    $escaped = $keyLines[$i].Replace('\', '\\').Replace('"', '\"')
    $newLine = '    "' + $escaped + '\n"'
    if ($i -lt ($keyLines.Count - 1)) {
        $newLine += ' \'
    }
    $newMacro += $newLine
}

$output = @()
if ($start -gt 0) {
    $output += $secretLines[0..($start - 1)]
}
$output += $newMacro
if (($end + 1) -lt $secretLines.Count) {
    $output += $secretLines[($end + 1)..($secretLines.Count - 1)]
}

Set-Content -LiteralPath $resolvedSecrets.Path -Encoding UTF8 -Value $output
Write-Host "Updated PrivateKey in netease_secrets.h. The key content was not printed."
