$ErrorActionPreference = 'Stop'
$base = $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$tempDir = Join-Path $env:TEMP ("GenshinAccountSwitcher-Diagnostic-" + $stamp)
$zipPath = Join-Path $base ("GenshinAccountSwitcher-Diagnostic-" + $stamp + ".zip")

try {
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

    $startupLog = Join-Path $base 'data\logs\startup.log'
    if (Test-Path -LiteralPath $startupLog) {
        Copy-Item -LiteralPath $startupLog -Destination (Join-Path $tempDir 'startup.log')
    } else {
        'startup.log was not created. The failure may have occurred before the application logger initialized, or the directory may not be writable.' |
            Set-Content -LiteralPath (Join-Path $tempDir 'startup-log-missing.txt') -Encoding UTF8
    }

    $processExe = Join-Path $base 'GenshinAccountSwitcher.exe'
    $version = [ordered]@{
        CollectedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        OperatingSystem = [Environment]::OSVersion.VersionString
        Is64BitOS = [Environment]::Is64BitOperatingSystem
        Is64BitProcess = [Environment]::Is64BitProcess
        AppPresent = (Test-Path -LiteralPath $processExe)
    }
    if (Test-Path -LiteralPath $processExe) {
        $version['ExeSHA256'] = (Get-FileHash -LiteralPath $processExe -Algorithm SHA256).Hash
        $version['FileVersion'] = (Get-Item -LiteralPath $processExe).VersionInfo.FileVersion
    }
    $version | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $tempDir 'environment.json') -Encoding UTF8

    try {
        $since = (Get-Date).AddHours(-2)
        $events = @(
            Get-WinEvent -FilterHashtable @{
                LogName = 'Application'
                Id = 1000,1001,1026
                StartTime = $since
            } -ErrorAction Stop |
            Where-Object {
                $_.Message -match 'GenshinAccountSwitcher|Microsoft.UI.Xaml|WindowsAppRuntime'
            } |
            Select-Object TimeCreated, ProviderName, Id, LevelDisplayName, Message
        )
        ConvertTo-Json -InputObject $events -Depth 4 |
            Set-Content -LiteralPath (Join-Path $tempDir 'application-events.json') -Encoding UTF8
    } catch {
        'Application events could not be read: ' + $_.Exception.GetType().Name |
            Set-Content -LiteralPath (Join-Path $tempDir 'application-events-error.txt') -Encoding UTF8
    }

    Compress-Archive -Path (Join-Path $tempDir '*') -DestinationPath $zipPath -Force
    Write-Host ''
    Write-Host 'Diagnostic package created:' -ForegroundColor Green
    Write-Host $zipPath
    Write-Host ''
    Write-Host 'Please review the ZIP before sharing. It contains startup events, exception text,'
    Write-Host 'Windows application errors, and executable version/hash information.'
    Write-Host 'Account credential files and full memory dumps are not collected.'
} catch {
    Write-Host 'Diagnostic collection failed:' -ForegroundColor Red
    Write-Host $_.Exception.Message
    exit 1
} finally {
    Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}
