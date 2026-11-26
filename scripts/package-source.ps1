[CmdletBinding()]
param([string]$Destination = 'dist\DriverSource-Source-and-Build.zip')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$destinationPath = if ([IO.Path]::IsPathRooted($Destination)) { $Destination } else { Join-Path $root $Destination }
$stage = Join-Path (Join-Path $root 'build') ('source-kit-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($stage)
try {
    foreach ($directory in @('native','database','scripts','tests')) {
        Copy-Item -LiteralPath (Join-Path $root $directory) -Destination $stage -Recurse -Force
    }
    foreach ($file in @('build-windows.bat','CMakeLists.txt','README.md','SOURCES.md','CHANGELOG.md','VERSION','.gitattributes')) {
        Copy-Item -LiteralPath (Join-Path $root $file) -Destination $stage -Force
    }
    $executable = Join-Path $root 'dist\DriverSource.exe'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'Build DriverSource.exe before packaging the source kit.' }
    Copy-Item -LiteralPath $executable -Destination (Join-Path $stage 'DriverSource.exe') -Force
    $guide = @'
DriverSource C source and build kit

1. To run the tested application: start DriverSource.exe in this folder.
2. To compile: double-click build-windows.bat. Keep native/, scripts/, database/ together.
3. Build output: dist/DriverSource.exe and dist/database/.
4. Build log: build/build.log. The window stays open after a double-click build.
5. Compiler required: Windows x64 MinGW-w64 GCC (with windres) or MSVC + Windows SDK.
   Portable compiler folders: tools/mingw64/ or mingw64/.
6. Every stage prints progress; a waiting tool prints a heartbeat every 5 seconds.
   A single command times out after 300 seconds. Only that tool's process tree is stopped.
7. Command line: build-windows.bat mingw --no-pause
   or build-windows.bat msvc --no-pause
8. Diagnostic only: build-windows.bat --diagnose --no-pause
9. The app is native C. Python and CMake are optional maintenance/build alternatives.
10. The build launcher uses the Windows PowerShell included with Windows 10/11.
    It does not install a compiler or modify persistent execution policies.

See README.md for driver coverage, local matching and installation limitations.
'@
    [IO.File]::WriteAllText((Join-Path $stage 'BUILD-START-HERE.txt'),$guide,(New-Object Text.UTF8Encoding($true)))
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $destinationPath))
    if (Test-Path -LiteralPath $destinationPath) { Remove-Item -LiteralPath $destinationPath -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $destinationPath -Force
    $hash = (Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash.ToLower()
    [IO.File]::WriteAllText(($destinationPath + '.sha256'),($hash + '  ' + [IO.Path]::GetFileName($destinationPath) + "`r`n"),[Text.Encoding]::ASCII)
    Write-Host ('Source-and-build kit: ' + $destinationPath)
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
