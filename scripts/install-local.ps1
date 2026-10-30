$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$aviutl = Join-Path $root 'tools/aviutl2'
if (-not (Test-Path -LiteralPath (Join-Path $aviutl 'aviutl2.exe'))) {
    throw 'Extract the AviUtl2 ZIP into tools/aviutl2 first.'
}
$package = Join-Path $root 'dist/Script/Expression'
if (-not (Test-Path -LiteralPath (Join-Path $package 'Expression.mod2'))) {
    throw 'Run scripts/build.ps1 first.'
}
$scriptRoot = Join-Path $aviutl 'data/Script'
if (Get-Process aviutl2 -ErrorAction SilentlyContinue | Where-Object Path -EQ (Join-Path $aviutl 'aviutl2.exe')) {
    throw 'Close the test AviUtl2 before updating its plugins.'
}
New-Item -ItemType Directory -Force -Path $scriptRoot | Out-Null
Copy-Item -LiteralPath $package -Destination $scriptRoot -Recurse -Force
$pluginRoot = Join-Path $aviutl 'data/Plugin'
New-Item -ItemType Directory -Force -Path $pluginRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'dist/Plugin/Expression') -Destination $pluginRoot -Recurse -Force
Write-Output "Installed: $scriptRoot/Expression"
