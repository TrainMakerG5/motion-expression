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
        throw "アーカイブのSHA-256ハッシュが一致しません: $archive"
    }
    return $archive
}

$sdk = Get-VerifiedArchive 'https://spring-fragrance.mints.ne.jp/aviutl/aviutl2_sdk.zip' 'aviutl2_sdk-20261031.zip' 'EF95DF80378DA4F865813FAA40397BF56B00AA2F58CB54FFB2066CBAEDB6440B'
$quickjs = Get-VerifiedArchive 'https://api.github.com/repos/quickjs-ng/quickjs/tarball/v0.17.0' 'quickjs-ng-v0.17.0.tar.gz' '92212EA9A257A387A35ABCA50505D59AD9EDDDBA4580E064929D396511B89DCF'
Expand-Archive -LiteralPath $sdk -DestinationPath (Join-Path $dependencies 'aviutl2_sdk') -Force
if (-not (Test-Path -LiteralPath (Join-Path $dependencies 'quickjs-ng-quickjs-6d46d07/quickjs.h'))) {
    & tar -xzf $quickjs -C $dependencies
    if ($LASTEXITCODE -ne 0) { throw 'QuickJS-ngを展開できませんでした。' }
}
Write-Output '依存ファイルを準備しました。'
