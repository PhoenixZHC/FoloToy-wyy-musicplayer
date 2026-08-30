param(
    [string]$Name = ''
)

$ErrorActionPreference = 'Stop'

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$TempDir = Join-Path $env:TEMP ("netease-official-host-tests-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

try {
    $cc = $env:CC
    if ([string]::IsNullOrWhiteSpace($cc)) {
        $cmd = Get-Command gcc.exe -ErrorAction SilentlyContinue
        if ($cmd -eq $null) {
            throw 'gcc.exe is required for host tests. Set CC or install MinGW GCC.'
        }
        $cc = $cmd.Source
    }
    $cjsonDir = $env:CJSON_DIR
    if ([string]::IsNullOrWhiteSpace($cjsonDir)) {
        $cjsonDir = 'C:\esp\v5.5.4\esp-idf\components\json\cJSON'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $cjsonDir 'cJSON.c'))) {
        throw "cJSON source not found: $cjsonDir"
    }

    $tests = @(
        @{
            Name = 'official_api_codec'
            Args = @(
                '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-DNETEASE_OFFICIAL_HOST_TEST',
                '-I' + (Join-Path $ProjectDir '..\components\netease_official_client\include'),
                '-I' + $cjsonDir,
                (Join-Path $ProjectDir 'tests\test_official_api_codec.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\official_api_codec.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\official_http.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\official_api_signer.c'),
                (Join-Path $cjsonDir 'cJSON.c'),
                '-o', (Join-Path $TempDir 'test_official_api_codec.exe')
            )
            Exe = Join-Path $TempDir 'test_official_api_codec.exe'
        },
        @{
            Name = 'official_client_init'
            Args = @(
                '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-DNETEASE_OFFICIAL_HOST_TEST',
                '-I' + (Join-Path $ProjectDir '..\components\netease_official_client\include'),
                '-I' + $cjsonDir,
                (Join-Path $ProjectDir 'tests\test_official_client_init.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\netease_official_client.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\official_api_codec.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\official_http.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\official_api_signer.c'),
                (Join-Path $cjsonDir 'cJSON.c'),
                '-o', (Join-Path $TempDir 'test_official_client_init.exe')
            )
            Exe = Join-Path $TempDir 'test_official_client_init.exe'
        },
        @{
            Name = 'official_business_api'
            Args = @(
                '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-DNETEASE_OFFICIAL_HOST_TEST',
                '-I' + (Join-Path $ProjectDir '..\components\netease_official_client\include'),
                '-I' + $cjsonDir,
                (Join-Path $ProjectDir 'tests\test_official_business_api.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\netease_official_client.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\official_api_codec.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\official_http.c'),
                (Join-Path $ProjectDir '..\components\netease_official_client\official_api_signer.c'),
                (Join-Path $cjsonDir 'cJSON.c'),
                '-o', (Join-Path $TempDir 'test_official_business_api.exe')
            )
            Exe = Join-Path $TempDir 'test_official_business_api.exe'
        },
        @{
            Name = 'official_auth_logic'
            Args = @(
                '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-DNETEASE_OFFICIAL_HOST_TEST',
                '-I' + (Join-Path $ProjectDir 'main'),
                '-I' + (Join-Path $ProjectDir '..\components\netease_official_client\include'),
                (Join-Path $ProjectDir 'tests\test_official_auth_logic.c'),
                (Join-Path $ProjectDir 'main\official_auth_store.c'),
                '-o', (Join-Path $TempDir 'test_official_auth_logic.exe')
            )
            Exe = Join-Path $TempDir 'test_official_auth_logic.exe'
        },
        @{
            Name = 'wifi_provisioning_logic'
            Args = @(
                '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + (Join-Path $ProjectDir 'main'),
                (Join-Path $ProjectDir 'tests\test_wifi_provisioning_logic.c'),
                (Join-Path $ProjectDir 'main\wifi_provisioning_logic.c'),
                '-o', (Join-Path $TempDir 'test_wifi_provisioning_logic.exe')
            )
            Exe = Join-Path $TempDir 'test_wifi_provisioning_logic.exe'
        }
    )

    $selected = $tests
    if (-not [string]::IsNullOrWhiteSpace($Name)) {
        $selected = @($tests | Where-Object { $_.Name -eq $Name })
        if ($selected.Count -eq 0) {
            throw "Unknown host test: $Name"
        }
    }

    foreach ($test in $selected) {
        & $cc @($test.Args)
        if ($LASTEXITCODE -ne 0) {
            throw "Compile failed: $($test.Name)"
        }
        & $test.Exe
        if ($LASTEXITCODE -ne 0) {
            throw "Test failed: $($test.Name)"
        }
    }

    Write-Output "Host tests: PASS ($($selected.Count))"
}
finally {
    if (Test-Path -LiteralPath $TempDir) {
        Remove-Item -LiteralPath $TempDir -Recurse -Force
    }
}
