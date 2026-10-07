# lang_smoke.ps1 - 多语言 GUI 端到端冒烟测试
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File tools\lang_smoke.ps1
# 覆盖：默认英文(无设置文件) -> 点击按钮切中文 -> 重启验证持久化 -> 切回英文
# 注意：需先构建 build\BiliLiveCpp.exe；脚本会临时删除 bili_settings.ini 并在结束时恢复
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class LW {
    public delegate bool EnumProc(IntPtr h, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
}
"@

function GetTitle([IntPtr]$h) {
    $sb = New-Object System.Text.StringBuilder 256
    [void][LW]::GetWindowText($h, $sb, 256)
    $sb.ToString()
}
function GetClassNameOf([IntPtr]$h) {
    $sb = New-Object System.Text.StringBuilder 256
    [void][LW]::GetClassName($h, $sb, 256)
    $sb.ToString()
}
function FindTopByPid([int]$targetPid, [string]$cls) {
    $script:found = [IntPtr]::Zero
    $cb = [LW+EnumProc]{
        param($h, $lp)
        $p = 0
        [void][LW]::GetWindowThreadProcessId($h, [ref]$p)
        if ($p -eq $targetPid -and (GetClassNameOf $h) -eq $cls) { $script:found = $h; return $false }
        return $true
    }
    [void][LW]::EnumWindows($cb, [IntPtr]::Zero)
    return $script:found
}
function FindChildById([IntPtr]$parent, [int]$id) {
    $script:found = [IntPtr]::Zero
    $cb = [LW+EnumProc]{
        param($h, $lp)
        if ([LW]::GetDlgCtrlID($h) -eq $id) { $script:found = $h; return $false }
        return $true
    }
    [void][LW]::EnumChildWindows($parent, $cb, [IntPtr]::Zero)
    return $script:found
}
function WaitTop([int]$targetPid, [string]$cls, [int]$tries) {
    for ($i = 0; $i -lt $tries; $i++) {
        $h = FindTopByPid $targetPid $cls
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 400
    }
    return [IntPtr]::Zero
}

$root = Split-Path -Parent $PSScriptRoot  # tools/ 的上一级 = 工程根目录
$exe = Join-Path $root 'build\BiliLiveCpp.exe'
$settings = Join-Path $root 'build\bili_settings.ini'
$fail = @()

# 记录原始设置，测试结束后恢复（本脚本会反复改写该文件）
$origSettings = if (Test-Path $settings) { [System.IO.File]::ReadAllText($settings) } else { $null }

# ---- 阶段 0：删除设置文件，验证"默认英文" ----
if (Test-Path $settings) { Remove-Item $settings -Force }
$p = Start-Process -FilePath $exe -PassThru
$h = WaitTop $p.Id 'BiliLiveLoginWnd' 25
if ($h -eq [IntPtr]::Zero) { Write-Host 'FAIL: phase0 window not found'; Stop-Process -Id $p.Id -EA SilentlyContinue; exit 1 }
$t0 = GetTitle $h
Write-Host "phase0 default title: $t0"
if ($t0 -ne 'bilibili Sign-in (QR Scan)') { $fail += "default-title=$t0" }
if (Test-Path $settings) { $fail += 'settings-exists-after-default-start' }

# ---- 阶段 1：点击"中文"按钮 -> 中文界面 + 设置持久化 ----
$btn = FindChildById $h 2006
if ($btn -eq [IntPtr]::Zero) { $fail += 'lang-button-not-found' }
else {
    $btnText = GetTitle $btn
    Write-Host "lang button text (en state): $btnText"
    if ($btnText -ne '中文') { $fail += "btn-text-en-state=$btnText" }
    [void][LW]::SendMessage($btn, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)  # BM_CLICK
    Start-Sleep -Milliseconds 800
    $t1 = GetTitle $h
    $s1 = if (Test-Path $settings) { (Get-Content $settings -Raw).Trim() } else { '(missing)' }
    Write-Host "phase1 zh title: $t1 | settings: $s1"
    if ($t1 -ne 'bilibili 扫码登录') { $fail += "zh-title=$t1" }
    if ($s1 -ne 'lang=zh') { $fail += "zh-settings=$s1" }
}
Stop-Process -Id $p.Id -EA SilentlyContinue
Start-Sleep -Milliseconds 600

# ---- 阶段 2：重启 -> 读取持久化设置，应直接是中文 ----
$p2 = Start-Process -FilePath $exe -PassThru
$h2 = WaitTop $p2.Id 'BiliLiveLoginWnd' 25
if ($h2 -eq [IntPtr]::Zero) { Write-Host 'FAIL: phase2 window not found'; Stop-Process -Id $p2.Id -EA SilentlyContinue; exit 1 }
$t2 = GetTitle $h2
$btn2 = FindChildById $h2 2006
$btn2Text = if ($btn2 -ne [IntPtr]::Zero) { GetTitle $btn2 } else { '(missing)' }
Write-Host "phase2 after-restart title: $t2 | button: $btn2Text"
if ($t2 -ne 'bilibili 扫码登录') { $fail += "persist-title=$t2" }
if ($btn2Text -ne 'English') { $fail += "btn-text-zh-state=$btn2Text" }

# ---- 阶段 3：点击"English"按钮 -> 切回英文 + 设置更新 ----
if ($btn2 -ne [IntPtr]::Zero) {
    [void][LW]::SendMessage($btn2, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800
    $t3 = GetTitle $h2
    $s3 = (Get-Content $settings -Raw).Trim()
    Write-Host "phase3 en title: $t3 | settings: $s3"
    if ($t3 -ne 'bilibili Sign-in (QR Scan)') { $fail += "en-title=$t3" }
    if ($s3 -ne 'lang=en') { $fail += "en-settings=$s3" }
}
Stop-Process -Id $p2.Id -EA SilentlyContinue

# 恢复原始设置状态
if ($null -ne $origSettings) { [System.IO.File]::WriteAllText($settings, $origSettings) }
elseif (Test-Path $settings) { Remove-Item $settings -Force }

if ($fail.Count -eq 0) { Write-Host 'RESULT: SMOKE PASS' ; exit 0 }
Write-Host "RESULT: SMOKE FAIL -> $($fail -join ', ')"
exit 1
