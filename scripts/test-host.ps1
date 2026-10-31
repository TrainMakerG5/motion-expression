param([string]$HostDirectory)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = if ($HostDirectory) { (Resolve-Path -LiteralPath $HostDirectory).Path } else { Join-Path $root 'tools/aviutl2' }
$sandbox = Join-Path $root 'tools/parameter-test'
$probe = Join-Path $root 'build/ninja/ExpressionProbe.aux2'
if (-not (Test-Path -LiteralPath $probe)) { throw 'Run scripts/build.ps1 first.' }
if (Get-Process aviutl2 -ErrorAction SilentlyContinue | Where-Object Path -EQ (Join-Path $sandbox 'aviutl2.exe')) {
    throw 'The isolated host-test copy is already running.'
}
New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
Get-ChildItem -LiteralPath $source -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $sandbox -Force
}
$data = Join-Path $sandbox 'data'
New-Item -ItemType Directory -Force -Path $data | Out-Null
foreach ($directory in @('Script', 'Plugin')) {
    Copy-Item -LiteralPath (Join-Path $root "dist/$directory") -Destination $data -Recurse -Force
}
Copy-Item -LiteralPath $probe -Destination (Join-Path $data 'Plugin/Expression/ExpressionProbe.aux2') -Force
$report = Join-Path $sandbox 'host-check.log'
Set-Content -LiteralPath $report -Value 'Waiting for the host probe.' -Encoding utf8
$previousOutput = $env:EXPRESSION_PROBE_OUTPUT
$env:EXPRESSION_PROBE_OUTPUT = $report
# 隔離した検証環境で、配置した自作ファイルだけを信頼済みにします。
$trustedFiles = @(
    'Expression\Expression.mod2', 'Expression\ExpressionEditor.aux2',
    'Expression\ExpressionProbe.aux2', 'Expression\JS制御.anm2',
    'Expression\JSテキスト.obj2', 'Expression\@Expression.tra2',
    'Expression\expression_track.lua', 'Expression\expression_effects.lua'
)
$trustFile = Join-Path $data 'module.ini'
$trustData = if (Test-Path -LiteralPath $trustFile) { [IO.File]::ReadAllText($trustFile) } else { '' }
foreach ($name in $trustedFiles) {
    $pattern = '(?ms)^\[' + [Regex]::Escape($name) + '\]\r?\n.*?(?=^\[|\z)'
    $entry = "[$name]`r`ntrust=1`r`n"
    if ([Regex]::IsMatch($trustData, $pattern)) {
        $trustData = [Regex]::Replace($trustData, $pattern, [Text.RegularExpressions.MatchEvaluator]{ param($match) $entry })
    } else {
        $trustData += "`r`n" + $entry
    }
}
[IO.File]::WriteAllText($trustFile, $trustData, [Text.UTF8Encoding]::new($false))

$hostProcess = $null
try {
    $hostProcess = Start-Process -FilePath (Join-Path $sandbox 'aviutl2.exe') -WorkingDirectory $sandbox -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 250
        $text = Get-Content -LiteralPath $report -Raw -Encoding utf8
        if ($text -match '(?m)^(PASS|FAIL)') { break }
    } while ([DateTime]::UtcNow -lt $deadline -and -not $hostProcess.HasExited)
    Write-Output $text
    if ($text -notmatch '(?m)^PASS') {
        Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ExpressionWindows {
    public delegate bool Callback(IntPtr hwnd, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, Callback callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
}
'@
        [ExpressionWindows]::EnumWindows({
            param($window, $parameter)
            [uint32]$windowProcess = 0
            [void][ExpressionWindows]::GetWindowThreadProcessId($window, [ref]$windowProcess)
            if ($windowProcess -eq $hostProcess.Id) {
                $caption = [Text.StringBuilder]::new(1024)
                [void][ExpressionWindows]::GetWindowText($window, $caption, 1024)
                Write-Host "Host window: $caption"
                [void][ExpressionWindows]::EnumChildWindows($window, {
                    param($child, $unused)
                    $label = [Text.StringBuilder]::new(4096)
                    [void][ExpressionWindows]::GetWindowText($child, $label, 4096)
                    if ($label.Length) { Write-Host "  $label" }
                    return $true
                }, [IntPtr]::Zero)
            }
            return $true
        }, [IntPtr]::Zero) | Out-Null
        throw "Host check did not pass. Report: $report"
    }
} finally {
    $env:EXPRESSION_PROBE_OUTPUT = $previousOutput
    if ($hostProcess -and -not $hostProcess.HasExited) {
        # このスクリプトが起動した検証用プロセスだけを終了します。
        Stop-Process -Id $hostProcess.Id
    }
}
