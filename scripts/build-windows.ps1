[CmdletBinding()]
param(
    [ValidateSet('auto','mingw','msvc')][string]$Toolchain = 'auto',
    [ValidateRange(1,3600)][int]$CommandTimeoutSeconds = 300,
    [ValidateRange(1,60)][int]$HeartbeatSeconds = 5,
    [switch]$Diagnose
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$distRoot = Join-Path $projectRoot 'dist'
$script:BuildExitCode = 0
$script:LastStage = '启动'
$script:LogWriter = $null
$script:BuildEnvironment = @{}
$buildVariables = @('PATH','PATHEXT','INCLUDE','LIB','LIBPATH','VSINSTALLDIR','VCINSTALLDIR',
    'VCToolsInstallDir','WindowsSdkDir','WindowsSDKVersion','WindowsSDKLibVersion',
    'UniversalCRTSdkDir','UCRTVersion','VSCMD_ARG_HOST_ARCH','VSCMD_ARG_TGT_ARCH')

function Write-BuildLine([string]$Text) {
    [Console]::WriteLine($Text)
    if ($script:LogWriter) {
        $script:LogWriter.WriteLine(('[{0}] {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'),$Text))
    }
}
function Quote-ProcessArgument([string]$Value) {
    if ($Value -and $Value -notmatch '[\s"]') { return $Value }
    $escaped = [regex]::Replace($Value, '(\\*)"', {param($match) $match.Groups[1].Value + $match.Groups[1].Value + '\"'})
    $escaped = [regex]::Replace($escaped, '(\\+)$', {param($match) $match.Value + $match.Value})
    return '"' + $escaped + '"'
}
function Stop-OwnedProcess($Process) {
    if ($Process.HasExited) { return }
    $killer = New-Object System.Diagnostics.ProcessStartInfo
    $killer.FileName = Join-Path $env:SystemRoot 'System32\taskkill.exe'
    $killer.Arguments = '/PID ' + $Process.Id + ' /T /F'
    $killer.UseShellExecute = $false
    $killer.CreateNoWindow = $true
    $killer.RedirectStandardOutput = $true
    $killer.RedirectStandardError = $true
    $killProcess = [Diagnostics.Process]::Start($killer)
    if (-not $killProcess.WaitForExit(10000)) { $killProcess.Kill() }
    $killProcess.Dispose()
    if (-not $Process.WaitForExit(5000)) { $Process.Kill() }
}
function Invoke-BuildTool {
    param([string]$FilePath, [string[]]$ToolArguments, [string]$Stage,
          [string]$WorkingDirectory = $projectRoot, [int]$TimeoutSeconds = $CommandTimeoutSeconds,
          [switch]$CaptureBuildEnvironment)
    $script:LastStage = $Stage
    Write-BuildLine ('[步骤] ' + $Stage)
    $commandLine = ($ToolArguments | ForEach-Object { Quote-ProcessArgument $_ }) -join ' '
    Write-BuildLine ('> "' + $FilePath + '" ' + $commandLine)
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $FilePath
    $info.Arguments = $commandLine
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.RedirectStandardInput = $true
    $info.StandardOutputEncoding = New-Object Text.UTF8Encoding($false)
    $info.StandardErrorEncoding = New-Object Text.UTF8Encoding($false)
    $info.EnvironmentVariables['VSLANG'] = '1033'
    $info.EnvironmentVariables['LC_ALL'] = 'C'
    foreach ($name in $script:BuildEnvironment.Keys) { $info.EnvironmentVariables[$name] = $script:BuildEnvironment[$name] }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    $stdout = New-Object 'Collections.Generic.List[string]'
    $stderr = New-Object 'Collections.Generic.List[string]'
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) { throw '无法启动工具。' }
        Write-BuildLine ('[进程] PID=' + $process.Id)
        $process.StandardInput.Close()
        $stdoutTask = $process.StandardOutput.ReadLineAsync()
        $stderrTask = $process.StandardError.ReadLineAsync()
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $lastHeartbeat = 0
        while (-not $process.HasExited -or $null -ne $stdoutTask -or $null -ne $stderrTask) {
            if ($null -ne $stdoutTask -and $stdoutTask.IsCompleted) {
                $line = $stdoutTask.GetAwaiter().GetResult()
                if ($null -eq $line) { $stdoutTask = $null }
                else {
                    $isBuildVariable = $false
                    if ($CaptureBuildEnvironment -and $line -match '^([^=]+)=(.*)$') {
                        $variable = $Matches[1]; $value = $Matches[2]
                        $isBuildVariable = $true
                        if ($buildVariables -contains $variable) {
                            $script:BuildEnvironment[$variable] = $value
                        }
                    }
                    if (-not $isBuildVariable) { $stdout.Add($line); Write-BuildLine $line }
                    $stdoutTask = $process.StandardOutput.ReadLineAsync()
                }
            }
            if ($null -ne $stderrTask -and $stderrTask.IsCompleted) {
                $line = $stderrTask.GetAwaiter().GetResult()
                if ($null -eq $line) { $stderrTask = $null }
                else {
                    $stderr.Add($line); Write-BuildLine $line
                    $stderrTask = $process.StandardError.ReadLineAsync()
                }
            }
            if (-not $process.HasExited -and $timer.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                Write-BuildLine ('[TIMEOUT] 工具超过 {0} 秒没有结束，停止本次启动的工具进程。' -f $TimeoutSeconds)
                Stop-OwnedProcess $process
                return [pscustomobject]@{ExitCode=124; Stdout=$stdout.ToArray(); Stderr=$stderr.ToArray(); TimedOut=$true}
            }
            if ($timer.Elapsed.TotalSeconds - $lastHeartbeat -ge $HeartbeatSeconds) {
                Write-BuildLine ('[等待] {0}，已运行 {1} 秒；日志仍在记录。' -f $Stage,[int]$timer.Elapsed.TotalSeconds)
                $lastHeartbeat = $timer.Elapsed.TotalSeconds
            }
            Start-Sleep -Milliseconds 20
        }
        $process.WaitForExit()
        Write-BuildLine ('[结果] ExitCode={0}，用时 {1:N1} 秒' -f $process.ExitCode,$timer.Elapsed.TotalSeconds)
        return [pscustomobject]@{ExitCode=$process.ExitCode; Stdout=$stdout.ToArray(); Stderr=$stderr.ToArray(); TimedOut=$false}
    }
    finally {
        if ($started -and -not $process.HasExited) { Stop-OwnedProcess $process }
        $process.Dispose()
    }
}
function Require-Success($Result, [string]$Message) {
    if ($Result.ExitCode -ne 0) {
        $script:BuildExitCode = [int]$Result.ExitCode
        throw ($Message + ' ExitCode=' + $Result.ExitCode)
    }
}
function Find-BuildExecutable([string]$Name) {
    $searchPath = $env:PATH
    if ($script:BuildEnvironment.ContainsKey('PATH')) { $searchPath = $script:BuildEnvironment['PATH'] }
    foreach ($directory in $searchPath.Split(';')) {
        $directory = $directory.Trim().Trim('"')
        if (-not $directory) { continue }
        $candidate = Join-Path $directory $Name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
    }
    return $null
}
function Find-Mingw {
    $candidates = New-Object 'Collections.Generic.List[string]'
    if ($env:DRIVERSOURCE_GCC) { $candidates.Add($env:DRIVERSOURCE_GCC) }
    else {
        foreach ($relative in @('tools\mingw64\bin\gcc.exe','mingw64\bin\gcc.exe')) {
            $candidate = Join-Path $projectRoot $relative
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $candidates.Add($candidate) }
        }
        $pathGcc = Find-BuildExecutable 'gcc.exe'
        if ($pathGcc -and -not $candidates.Contains($pathGcc)) { $candidates.Add($pathGcc) }
    }
    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            Write-BuildLine ('[未找到] ' + $candidate); continue
        }
        $candidate = (Get-Item -LiteralPath $candidate).FullName
        $probe = Invoke-BuildTool $candidate @('-dumpmachine') '识别 GCC 目标架构' -TimeoutSeconds ([Math]::Min($CommandTimeoutSeconds,30))
        if ($probe.ExitCode -ne 0 -and ($Toolchain -eq 'mingw' -or $env:DRIVERSOURCE_GCC)) {
            Require-Success $probe 'GCC 检测失败，请检查该编译器能否正常运行。'
        }
        if ($probe.ExitCode -eq 0 -and ($probe.Stdout -join '').Trim() -eq 'x86_64-w64-mingw32') { return $candidate }
        Write-BuildLine '[跳过] 该 GCC 不是可用的 Windows x64 MinGW-w64 编译器。'
    }
    return $null
}
function Initialize-Msvc {
    $cl = Find-BuildExecutable 'cl.exe'
    if ($cl -and $env:INCLUDE -and $env:LIB -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return $cl }
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $locator -PathType Leaf)) { return $null }
    $found = Invoke-BuildTool $locator @('-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath') '查找 Visual Studio C 工具链' -TimeoutSeconds ([Math]::Min($CommandTimeoutSeconds,30))
    Require-Success $found 'Visual Studio 查询失败。'
    $installation = $found.Stdout | Where-Object { $_.Trim() } | Select-Object -First 1
    if (-not $installation) { return $null }
    $vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) { return $null }
    # Query only compiler-related variables, never dump the process environment.
    $lines = @('@echo off','chcp 65001 >nul',('call "' + $vcvars + '"'), 'if errorlevel 1 exit /b %errorlevel%')
    foreach ($name in $buildVariables) { $lines += 'set ' + $name }
    $lines += 'exit /b 0'
    [IO.File]::WriteAllText((Join-Path $buildRoot 'initialize-msvc.cmd'),($lines -join "`r`n") + "`r`n",(New-Object Text.UTF8Encoding($false)))
    $initialized = Invoke-BuildTool (Join-Path $env:SystemRoot 'System32\cmd.exe') @('/d','/c','build\initialize-msvc.cmd') '初始化 MSVC x64 环境' -CaptureBuildEnvironment
    Require-Success $initialized 'MSVC 初始化失败，请检查 C++ Build Tools 和 Windows SDK。'
    Write-BuildLine ('[环境] 已导入 {0} 项编译环境设置。' -f $script:BuildEnvironment.Count)
    return Find-BuildExecutable 'cl.exe'
}

try {
    try { [Console]::OutputEncoding = New-Object Text.UTF8Encoding($false) } catch { }
    [void][IO.Directory]::CreateDirectory($buildRoot)
    $log = Join-Path $buildRoot 'build.log'
    $script:LogWriter = New-Object IO.StreamWriter($log,$false,(New-Object Text.UTF8Encoding($true)))
    $script:LogWriter.AutoFlush = $true
    Write-BuildLine ('DriverSource C 编译入口 · PowerShell ' + $PSVersionTable.PSVersion)
    Write-BuildLine ('项目目录：' + $projectRoot)
    Write-BuildLine ('日志文件：' + $log)
    $sources = @('main','driver_status','installed_driver','driver_backup','device_db','hardware_database','vendor_catalog','oem_catalog','vendor_download','update_diagnostics')
    $script:LastStage = '检查源码和数据库'
    foreach ($relative in (@('native\app.rc','native\app.manifest','database\vendor-packages.tsv','database\oem-packages.tsv') + ($sources | ForEach-Object { 'native\' + $_ + '.c' }))) {
        if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $relative) -PathType Leaf)) {
            throw ('缺少文件 ' + $relative + '。请完整解压 Source-and-Build 源码包；运行包不包含编译所需源码。')
        }
    }
    $gcc = $null; $cl = $null
    if ($Toolchain -ne 'msvc') { $gcc = Find-Mingw }
    if (-not $gcc -and $Toolchain -ne 'mingw') { $cl = Initialize-Msvc }
    if (-not $gcc -and -not $cl) {
        $script:BuildExitCode = 2
        throw '未找到可用 C 编译器。请将 Windows x64 MinGW-w64 放到 tools\mingw64，或安装带 Windows SDK 的 Visual Studio 2022 Build Tools。'
    }
    if ($gcc) {
        $selected = 'mingw'
        $script:BuildEnvironment['PATH'] = (Split-Path -Parent $gcc) + ';' + $env:PATH
        $resourceCompiler = Join-Path (Split-Path -Parent $gcc) 'windres.exe'
        if (-not (Test-Path -LiteralPath $resourceCompiler -PathType Leaf)) { throw 'GCC 同目录缺少 windres.exe，请使用完整的 MinGW-w64 工具链。' }
        Require-Success (Invoke-BuildTool $gcc @('--version') '显示 GCC 版本') 'GCC 无法运行。'
    } else {
        $selected = 'msvc'
        $resourceCompiler = Find-BuildExecutable 'rc.exe'
        $linker = Join-Path (Split-Path -Parent $cl) 'link.exe'
        if (-not $resourceCompiler -or -not (Test-Path -LiteralPath $linker -PathType Leaf)) { throw '缺少 rc.exe 或 MSVC link.exe，请安装 Windows SDK 并使用 x64 C 工具链。' }
    }
    Write-BuildLine ('[工具链] ' + $selected)
    if ($Diagnose) {
        Write-BuildLine '[完成] 工具链检测通过，诊断模式未编译程序。'
    } else {
        [void][IO.Directory]::CreateDirectory($distRoot)
        [void][IO.Directory]::CreateDirectory((Join-Path $buildRoot $selected))
        $objects = @()
        if ($gcc) {
            Require-Success (Invoke-BuildTool $resourceCompiler @('-I','native','native\app.rc','-o','build\mingw\app.o') '[1/12] 编译 Windows 资源') '资源编译失败。'
            for ($i=0; $i -lt $sources.Count; ++$i) {
                $object = 'build\mingw\' + $sources[$i] + '.o'; $objects += $object
                $arguments = @('-std=c17','-Wall','-Wextra','-Werror','-finput-charset=UTF-8','-O2','-c',('native\'+$sources[$i]+'.c'),'-o',$object)
                Require-Success (Invoke-BuildTool $gcc $arguments ('[{0}/12] 编译 {1}.c' -f ($i+2),$sources[$i])) 'C 编译失败，请查看上方第一条编译错误。'
            }
            $libraries = @('setupapi','cfgmgr32','ole32','oleaut32','comctl32','shell32','shlwapi','advapi32','user32','gdi32','uuid','winhttp','bcrypt') | ForEach-Object { '-l' + $_ }
            Require-Success (Invoke-BuildTool $gcc (@('-municode','-mwindows') + $objects + @('build\mingw\app.o','-o','dist\DriverSource.exe') + $libraries) '[12/12] 链接 DriverSource.exe') '链接失败，检查目标 EXE 是否仍在运行或被其他程序占用。'
        } else {
            Require-Success (Invoke-BuildTool $resourceCompiler @('/nologo','/fo','..\build\msvc\app.res','app.rc') '[1/12] 编译 Windows 资源' -WorkingDirectory (Join-Path $projectRoot 'native')) '资源编译失败。'
            for ($i=0; $i -lt $sources.Count; ++$i) {
                $object = 'build\msvc\' + $sources[$i] + '.obj'; $objects += $object
                $arguments = @('/nologo','/TC','/std:c17','/utf-8','/W4','/WX','/O2','/MT','/D_CRT_SECURE_NO_WARNINGS','/c',('native\'+$sources[$i]+'.c'),('/Fo'+$object))
                Require-Success (Invoke-BuildTool $cl $arguments ('[{0}/12] 编译 {1}.c' -f ($i+2),$sources[$i])) 'C 编译失败，请查看上方第一条编译错误。'
            }
            $libraries = @('setupapi','cfgmgr32','ole32','oleaut32','comctl32','shell32','shlwapi','advapi32','user32','gdi32','uuid','winhttp','bcrypt') | ForEach-Object { $_ + '.lib' }
            Require-Success (Invoke-BuildTool $linker (@('/nologo','/SUBSYSTEM:WINDOWS','/MACHINE:X64','/MANIFEST:NO','/OUT:dist\DriverSource.exe') + $objects + @('build\msvc\app.res') + $libraries) '[12/12] 链接 DriverSource.exe') '链接失败，检查目标 EXE 是否仍在运行或被其他程序占用。'
        }
        $script:LastStage = '复制驱动目录'
        Write-BuildLine '[数据] 复制离线硬件资料和厂商包目录…'
        Copy-Item -LiteralPath (Join-Path $projectRoot 'database') -Destination $distRoot -Recurse -Force
        Write-BuildLine ('[成功] 编译完成：' + (Join-Path $distRoot 'DriverSource.exe'))
    }
    $script:BuildExitCode = 0
} catch {
    if ($script:BuildExitCode -eq 0) { $script:BuildExitCode = 1 }
    Write-BuildLine ('[失败步骤] ' + $script:LastStage)
    Write-BuildLine ('[错误] ' + $_.Exception.Message)
    Write-BuildLine '[提示] 日志保存在 build\build.log。请提供最后一个步骤及第一条 error；Ctrl+C 可取消当前编译。'
} finally {
    if ($script:LogWriter) { $script:LogWriter.Dispose(); $script:LogWriter = $null }
}
exit $script:BuildExitCode
