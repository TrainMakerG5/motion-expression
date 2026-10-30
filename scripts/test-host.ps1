$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $root 'tools/aviutl2'
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
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ExpressionTrustWindows {
    public delegate bool Callback(IntPtr hwnd, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, Callback callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);
}
'@

function Confirm-BuiltPlugin {
    param([int]$ProcessId)
    [ExpressionTrustWindows]::EnumWindows({
        param($window, $unused)
        [uint32]$windowProcess = 0
        [void][ExpressionTrustWindows]::GetWindowThreadProcessId($window, [ref]$windowProcess)
        if ($windowProcess -ne $ProcessId) { return $true }
        $caption = [Text.StringBuilder]::new(1024)
        [void][ExpressionTrustWindows]::GetWindowText($window, $caption, 1024)
        if ($caption.ToString() -ne 'スクリプト・プラグインの追加') { return $true }
        $children = [Collections.Generic.List[object]]::new()
        [void][ExpressionTrustWindows]::EnumChildWindows($window, {
            param($child, $parameter)
            $label = [Text.StringBuilder]::new(4096)
            [void][ExpressionTrustWindows]::GetWindowText($child, $label, 4096)
            $children.Add(@{Window = $child; Label = $label.ToString().Trim()})
            return $true
        }, [IntPtr]::Zero)
        $allowedFiles = @('Expression\Expression.mod2', 'Expression\ExpressionEditor.aux2', 'Expression\ExpressionProbe.aux2', 'Expression\JS制御.anm2', 'Expression\@Expression.tra2', 'Expression\expression_track.lua')
        $fileLabels = @($children | Where-Object { $_.Label -match '\.(mod2|aux2|anm2|tra2|lua)$' })
        $trust = @($children | Where-Object Label -EQ 'このプラグイン・スクリプトを信頼して使用する')
        # 検証用コピーに配置した自作ファイルだけを確認します。
        if ($fileLabels.Count -gt 0 -and $trust.Count -eq 1) {
            $unknown = @($fileLabels | Where-Object { $_.Label -notin $allowedFiles })
            if ($unknown.Count -eq 0) {
                Write-Host ('Trusting built test files: ' + (($fileLabels | ForEach-Object Label) -join ', '))
                [void][ExpressionTrustWindows]::SendMessage($trust[0].Window, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
}

$hostProcess = $null
try {
    $hostProcess = Start-Process -FilePath (Join-Path $sandbox 'aviutl2.exe') -WorkingDirectory $sandbox -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 250
        Confirm-BuiltPlugin -ProcessId $hostProcess.Id
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
