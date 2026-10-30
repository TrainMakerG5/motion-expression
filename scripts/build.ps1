$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmake) {
    if (Test-Path -LiteralPath $vswhere) {
        $installations = & $vswhere -products '*' -property installationPath
        foreach ($installation in $installations) {
            $candidate = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
            if (Test-Path -LiteralPath $candidate) { $cmake = $candidate; break }
        }
    }
}
if (-not $cmake) { throw 'CMake 3.24+ is required.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio C++ tools are required.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
$ninjaCommand = Get-Command ninja -ErrorAction SilentlyContinue
$ninja = if ($ninjaCommand) { $ninjaCommand.Source } else {
    Join-Path (Split-Path (Split-Path (Split-Path $cmake -Parent) -Parent) -Parent) 'Ninja/ninja.exe'
}
if (-not (Test-Path -LiteralPath $ninja)) { throw 'Ninja is required.' }
& (Join-Path $PSScriptRoot 'build-native.cmd') $vcvars $cmake $ninja $root
if ($LASTEXITCODE -ne 0) { throw 'Build or verification failed.' }
Write-Output 'Build and checks complete. Package: dist/'
