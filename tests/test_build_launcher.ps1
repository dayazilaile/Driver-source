$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
[void][IO.Directory]::CreateDirectory($build)
$fake = Join-Path $build 'fake-gcc.exe'
$source = @'
using System;
using System.Diagnostics;
using System.Threading;
public static class FakeCompiler {
    public static int Main(string[] args) {
        string mode = Environment.GetEnvironmentVariable("DRIVERSOURCE_FAKE_MODE");
        if (mode == "hang") {
            var info = new ProcessStartInfo();
            info.FileName = Environment.GetEnvironmentVariable("SystemRoot") + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
            info.Arguments = "-NoLogo -NoProfile -NonInteractive -Command \"Start-Sleep -Seconds 60\"";
            info.UseShellExecute = false; info.CreateNoWindow = true;
            var child = Process.Start(info);
            Console.WriteLine("SIMULATED_CHILD_PID=" + child.Id);
            Thread.Sleep(60000); return 0;
        }
        Console.Error.WriteLine("SIMULATED_COMPILER_ERROR");
        return 33;
    }
}
'@
if (Test-Path -LiteralPath $fake) { Remove-Item -LiteralPath $fake -Force }
Add-Type -TypeDefinition $source -OutputAssembly $fake -OutputType ConsoleApplication
$ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$oldGcc = $env:DRIVERSOURCE_GCC
$oldMode = $env:DRIVERSOURCE_FAKE_MODE
function Run-Check([string]$Mode, [int]$Expected) {
    $env:DRIVERSOURCE_GCC = $fake
    $env:DRIVERSOURCE_FAKE_MODE = $Mode
    $stdout = Join-Path $build ('launcher-' + $Mode + '.stdout.log')
    $stderr = Join-Path $build ('launcher-' + $Mode + '.stderr.log')
    $arguments = '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + (Join-Path $root 'scripts\build-windows.ps1') + '" -Toolchain mingw -CommandTimeoutSeconds 4 -HeartbeatSeconds 1'
    $process = Start-Process $ps -ArgumentList $arguments -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $process.Handle
    if (-not $process.WaitForExit(45000)) {
        & "$env:SystemRoot/System32/taskkill.exe" /PID $process.Id /T /F | Out-Null
        throw 'Build controller itself hung.'
    }
    # Refresh ExitCode after redirected streams and process termination settle.
    $process.WaitForExit()
    $code = $process.ExitCode
    $process.Dispose()
    if ($code -ne $Expected) {
        Get-Content -LiteralPath $stdout -Encoding UTF8
        Get-Content -LiteralPath $stderr -Encoding UTF8
        throw "Expected $Expected, got $code"
    }
    $log = Get-Content -LiteralPath (Join-Path $build 'build.log') -Raw -Encoding UTF8
    if ($Mode -eq 'hang') {
        if ($log -notmatch 'TIMEOUT' -or $log -notmatch '\[等待\]') { throw 'Missing timeout/heartbeat diagnostic.' }
        $owned = [regex]::Matches($log, '(?:PID=|SIMULATED_CHILD_PID=)([0-9]+)')
        if ($owned.Count -lt 2) { throw 'Expected parent and child process diagnostics.' }
        foreach ($match in $owned) {
            if (Get-Process -Id ([int]$match.Groups[1].Value) -ErrorAction SilentlyContinue) { throw 'Owned compiler process survived timeout.' }
        }
    } elseif ($log -notmatch 'SIMULATED_COMPILER_ERROR' -or $log -notmatch 'ExitCode=33') {
        throw 'Compiler error or original exit code was lost.'
    }
    Copy-Item -LiteralPath (Join-Path $build 'build.log') -Destination (Join-Path $build ('launcher-' + $Mode + '.build.log')) -Force
}
try {
    Run-Check 'fail' 33
    Run-Check 'hang' 124
    Write-Host 'Compiler failure, original exit code, heartbeat and owned-child timeout checks passed.'
} catch {
    Write-Output ('::error::' + $_.Exception.Message.Replace('%','%25'))
    throw
} finally {
    $env:DRIVERSOURCE_GCC = $oldGcc
    $env:DRIVERSOURCE_FAKE_MODE = $oldMode
}
