# Compila el firmware del SDF-Pico con el bundle de herramientas de la
# extension pico-vscode.
#
#   .\build.ps1            compila
#   .\build.ps1 -Clean     borra build/ y reconfigura desde cero
#
# No hace falta instalar nada: SDK 2.3.0, GCC 14.2, ninja, cmake, pioasm y
# picotool vienen en ~/.pico-sdk.

param([switch]$Clean)

$ErrorActionPreference = "Stop"

$Pico    = Join-Path $HOME ".pico-sdk"
$Sdk     = Join-Path $Pico "sdk\2.3.0"
$Tools   = Join-Path $Pico "toolchain\14_2_Rel1"
$Ninja   = Join-Path $Pico "ninja\v1.12.1\ninja.exe"
$CMake   = Join-Path $Pico "cmake\v3.31.5\bin\cmake.exe"
$Pioasm  = Join-Path $Pico "tools\2.3.0\pioasm"
$Picotool= Join-Path $Pico "picotool\2.3.0\picotool"

foreach ($p in @($Sdk, $Tools, $Ninja, $CMake, $Pioasm, $Picotool)) {
    if (-not (Test-Path $p)) { throw "No encontrado: $p" }
}

$Root  = $PSScriptRoot
$Build = Join-Path $Root "build"

if ($Clean -and (Test-Path $Build)) {
    Remove-Item -Recurse -Force $Build
}

$env:PICO_SDK_PATH       = $Sdk
$env:PICO_TOOLCHAIN_PATH = $Tools
$env:PATH                = "$Tools\bin;" + (Split-Path $Ninja) + ";" + $env:PATH

if (-not (Test-Path (Join-Path $Build "build.ninja"))) {
    # pioasm y picotool son herramientas de HOST. Sin estas dos rutas, CMake
    # intenta compilarlas con el cross-compiler ARM y falla.
    & $CMake -S $Root -B $Build -G Ninja `
        -DPICO_PLATFORM=rp2350 `
        -DPICO_TOOLCHAIN_PATH="$Tools" `
        -DCMAKE_MAKE_PROGRAM="$Ninja" `
        -Dpioasm_DIR="$Pioasm" `
        -Dpicotool_DIR="$Picotool"
    if ($LASTEXITCODE -ne 0) { throw "Fallo la configuracion de CMake" }
}

& $Ninja -C $Build
if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion" }

& "$Tools\bin\arm-none-eabi-size.exe" (Join-Path $Build "sdf_pico.elf")
Write-Host ""
Write-Host "Listo: $Build\sdf_pico.uf2"
