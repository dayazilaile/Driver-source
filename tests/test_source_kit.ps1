param([ValidateSet('mingw','msvc')][string]$Toolchain)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $env:RUNNER_TEMP ('DriverSource 中文 build & ! (test) ' + [Guid]::NewGuid().ToString('N'))
try {
    & (Join-Path $root 'scripts\package-source.ps1')
    Expand-Archive -LiteralPath (Join-Path $root 'dist\DriverSource-Source-and-Build.zip') -DestinationPath $stage
    foreach ($relative in @('DriverSource.exe','native\main.c','scripts\build-windows.ps1','database\oem-packages.tsv','build-windows.bat')) {
        if (-not (Test-Path -LiteralPath (Join-Path $stage $relative))) { throw "Source kit is missing $relative" }
    }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = Join-Path $env:SystemRoot 'System32\cmd.exe'
    $info.Arguments = '/d /c build-windows.bat ' + $Toolchain + ' --no-pause'
    $info.WorkingDirectory = $stage
    $info.UseShellExecute = $false
    if ($Toolchain -eq 'msvc') {
        # Reproduce double-click startup outside a Visual Studio developer prompt.
        foreach ($name in @('INCLUDE','LIB','LIBPATH','VSCMD_ARG_TGT_ARCH','VSINSTALLDIR')) {
            $info.EnvironmentVariables.Remove($name)
        }
    }
    $process = [Diagnostics.Process]::Start($info)
    if (-not $process.WaitForExit(240000)) {
        & "$env:SystemRoot/System32/taskkill.exe" /PID $process.Id /T /F | Out-Null
        throw 'Source kit build timed out.'
    }
    if ($process.ExitCode -ne 0) { throw "Source kit build returned $($process.ExitCode)" }
    $process.Dispose()
    foreach ($argument in @('--self-test','--smoke-test')) {
        $info.FileName = Join-Path $stage 'dist\DriverSource.exe'
        $info.Arguments = $argument
        $process = [Diagnostics.Process]::Start($info)
        if (-not $process.WaitForExit(60000)) { $process.Kill(); throw "$argument timed out" }
        if ($process.ExitCode -ne 0) { throw "$argument failed with $($process.ExitCode)" }
        $process.Dispose()
    }
    Write-Host 'Extracted source kit builds and starts in a Chinese path containing spaces, &, ! and parentheses.'
} catch {
    $log = Join-Path $stage 'build\build.log'
    if (Test-Path -LiteralPath $log) {
        Get-Content -LiteralPath $log -Encoding UTF8 | Select-Object -Last 12 | ForEach-Object {
            Write-Output ('::error::' + $_.Replace('%','%25'))
        }
    }
    Write-Output ('::error::' + $_.Exception.Message.Replace('%','%25'))
    throw
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
