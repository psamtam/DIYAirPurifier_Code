param(
    [string]$ArduinoCli = 'arduino-cli',
    [switch]$InstallDependencies,
    [ValidateSet('Smooth','MinimumFlash')]
    [string]$CacheProfile = 'Smooth'
)
$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent $PSScriptRoot
function Invoke-Arduino {
    & $ArduinoCli @args
    if ($LASTEXITCODE -ne 0) { throw "arduino-cli failed with exit code $LASTEXITCODE" }
}
if ($InstallDependencies) {
    Invoke-Arduino core update-index
    Invoke-Arduino core install 'arduino:avr@1.8.8'
    Invoke-Arduino lib install 'LiquidCrystal I2C@1.1.2'
}
Invoke-Arduino version
Invoke-Arduino core list
Invoke-Arduino lib list
foreach ($sketch in @('AirPurifier','UiEepromProgrammer')) {
    $buildName = $sketch
    $compileArguments = @('compile','--fqbn','arduino:avr:leonardo','--warnings','all')
    if ($sketch -eq 'AirPurifier') {
        $buildName = "$sketch-$CacheProfile"
        $slots = if ($CacheProfile -eq 'MinimumFlash') { 1 } else { 6 }
        $compileArguments += @('--build-property',"compiler.cpp.extra_flags=-DUI_TEXT_CACHE_SLOTS=$slots")
    }
    $compileArguments += @('--build-path',(Join-Path $packageRoot "build\$buildName"),(Join-Path $packageRoot $sketch))
    Invoke-Arduino @compileArguments
}
# Build only. This script never uploads firmware or writes a connected device.
