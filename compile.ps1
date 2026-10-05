# ==============================================================================
# Build Script for Smart Irrigation System
# Compiles sketch.ino into Arduino Uno HEX & ELF binaries for Wokwi Simulator
# ==============================================================================

$ErrorActionPreference = "Stop"

# Locate arduino-cli
$cliPaths = @(
    "C:\Users\exobh\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
    "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
)

$arduinoCli = $null
foreach ($path in $cliPaths) {
    if (Test-Path $path) {
        $arduinoCli = $path
        break
    }
}

if (-not $arduinoCli) {
    $cmd = Get-Command "arduino-cli" -ErrorAction SilentlyContinue
    if ($cmd) {
        $arduinoCli = $cmd.Source
    }
}

if (-not $arduinoCli) {
    Write-Error "arduino-cli executable not found. Please install Arduino IDE or Arduino CLI."
    exit 1
}

$projectDir = $PSScriptRoot
if (-not $projectDir) { $projectDir = $pwd.Path }
$buildDir   = Join-Path $projectDir "build"
$tmpSketch  = Join-Path $buildDir "tmp_sketch\sketch"

Write-Host "Compiling sketch.ino for target: arduino:avr:uno (ATmega328P)..." -ForegroundColor Cyan

# Prepare isolated sketch directory required by Arduino CLI
New-Item -ItemType Directory -Path $tmpSketch -Force | Out-Null
Copy-Item (Join-Path $projectDir "sketch.ino") (Join-Path $tmpSketch "sketch.ino") -Force

try {
    & $arduinoCli compile --fqbn arduino:avr:uno --output-dir $buildDir $tmpSketch
    Write-Host "`nBuild successful! Binaries generated in ./build/:" -ForegroundColor Green
    Write-Host "  - build/sketch.ino.hex (Simulation Firmware)"
    Write-Host "  - build/sketch.ino.elf (Debugging Symbols)"
} finally {
    Remove-Item (Join-Path $buildDir "tmp_sketch") -Recurse -Force -ErrorAction SilentlyContinue
}
