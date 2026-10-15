# 让 ESP32-S3 退出下载模式并运行固件，然后读日志
param(
    [string] $Port = 'COM48',
    [int]    $Baud = 115200,
    [int]    $Seconds = 15,
    [switch] $ClassicReset
)

Write-Host '=== 复位并监听 ===' -ForegroundColor Cyan

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 300

try {
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.Open()
} catch {
    Write-Host "打开 $Port 失败: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Start-Sleep -Milliseconds 200

if ($ClassicReset) {
    # 经典 esptool 复位: 目的是【运行】而不是下载
    Write-Host '  执行经典复位时序 (RTS/DTR)...' -ForegroundColor DarkGray
    $sp.DtrEnable = $true;  $sp.RtsEnable = $true;  Start-Sleep -Milliseconds 100
    $sp.DtrEnable = $false; $sp.RtsEnable = $true;  Start-Sleep -Milliseconds 100
    $sp.DtrEnable = $false; $sp.RtsEnable = $false; Start-Sleep -Milliseconds 300
} else {
    # USB-Serial/JTAG: 把两路都放开, 再从复位状态释放
    Write-Host '  释放 DTR/RTS...' -ForegroundColor DarkGray
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    Start-Sleep -Milliseconds 500
}

# 清零输入缓冲后开始读
try { $sp.DiscardInBuffer() } catch { }

$buf = New-Object System.Text.StringBuilder
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    try { $ch = $sp.ReadExisting(); if ($ch) { [void]$buf.Append($ch) } } catch { }
    Start-Sleep -Milliseconds 100
}

$sp.Close(); $sp.Dispose()

$t = $buf.ToString()
if (-not $t.Trim()) {
    Write-Host '(无数据 —— 请手动按一下 SW1 复位键，然后立刻重跑本脚本)' -ForegroundColor Yellow
    exit 0
}

Write-Host "`n=== 收到 $($t.Length) 字符 ===" -ForegroundColor Green
Write-Host $t

Write-Host "`n=== 关键行检查 ===" -ForegroundColor Cyan
foreach ($pat in 'Set DIO2 as', 'RF switch, result', 'SX126x init result', 'Final Tx power',
                 'Use MCU pin', 'Frequency set to', 'waiting for download', 'DOWNLOAD') {
    $n = ([regex]::Matches($t, [regex]::Escape($pat))).Count
    if ($n) { Write-Host "  [命中] $pat  (x$n)" -ForegroundColor Green }
}
