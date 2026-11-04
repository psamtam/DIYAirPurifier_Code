param(
    [string]$ArduinoCli = 'arduino-cli',
    [switch]$InstallDependencies
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
    Invoke-Arduino compile --fqbn 'arduino:avr:leonardo' --warnings all --build-path (Join-Path $packageRoot "build\$sketch") (Join-Path $packageRoot $sketch)
}
# Build only. This script never uploads firmware or writes a connected device.
