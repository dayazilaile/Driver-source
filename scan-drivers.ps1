# Windows PowerShell 5.1 or PowerShell 7 on Windows. Read-only hardware scan.
$ErrorActionPreference = 'Stop'

$computer = Get-CimInstance -ClassName Win32_ComputerSystem
$os = Get-CimInstance -ClassName Win32_OperatingSystem
$devices = @(
    Get-CimInstance -ClassName Win32_PnPEntity -Filter 'Present = TRUE' |
        Where-Object { $_.Name -and $_.HardwareID } |
        Sort-Object Name |
        ForEach-Object {
            [ordered]@{
                name = $_.Name
                ids = @($_.HardwareID)
            }
        }
)

$report = [ordered]@{
    schemaVersion = 1
    computer = [ordered]@{
        manufacturer = $computer.Manufacturer
        model = $computer.Model
        os = $os.Caption
        architecture = $os.OSArchitecture
    }
    devices = $devices
}

$desktop = [Environment]::GetFolderPath('DesktopDirectory')
if (-not $desktop -or -not (Test-Path -LiteralPath $desktop -PathType Container)) {
    throw 'Desktop folder not found.'
}
$path = Join-Path $desktop 'driver-source-devices.json'
$encoding = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($path, ($report | ConvertTo-Json -Depth 5), $encoding)
Write-Output $path
