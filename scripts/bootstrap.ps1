$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dependencies = Join-Path $root 'third_party'
New-Item -ItemType Directory -Force -Path $dependencies | Out-Null

function Get-VerifiedArchive {
    param([string]$Url, [string]$Name, [string]$Hash)
    $archive = Join-Path $dependencies $Name
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -Uri $Url -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Hash) {
        throw "Archive checksum mismatch: $archive"
    }
    return $archive
}

$sdk = Get-VerifiedArchive 'https://spring-fragrance.mints.ne.jp/aviutl/aviutl2_sdk.zip' 'aviutl2_sdk.zip' '7A8733D9611FE26FE42671CD9FCD93CA07A77D5A412441DCFDABA017401B9499'
$quickjs = Get-VerifiedArchive 'https://api.github.com/repos/quickjs-ng/quickjs/tarball/v0.17.0' 'quickjs-ng-v0.17.0.tar.gz' '92212EA9A257A387A35ABCA50505D59AD9EDDDBA4580E064929D396511B89DCF'
if (-not (Test-Path -LiteralPath (Join-Path $dependencies 'aviutl2_sdk/module2.h'))) {
    Expand-Archive -LiteralPath $sdk -DestinationPath (Join-Path $dependencies 'aviutl2_sdk')
}
if (-not (Test-Path -LiteralPath (Join-Path $dependencies 'quickjs-ng-quickjs-6d46d07/quickjs.h'))) {
    & tar -xzf $quickjs -C $dependencies
    if ($LASTEXITCODE -ne 0) { throw 'QuickJS-ng extraction failed.' }
}
Write-Output 'Dependencies ready.'
