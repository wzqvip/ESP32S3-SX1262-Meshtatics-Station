# 抓取串口启动日志 (只读，不写板子)
param(
    [string] $Port = 'COM48',
    [int]    $Baud = 115200,
    [int]    $Seconds = 14
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 500
$sp.DtrEnable = $true      # 唤醒 / 允许运行
$sp.RtsEnable = $false

try {
    $sp.Open()
} catch {
    Write-Host "打开 $Port 失败: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Write-Host "=== 监听 $Port @ $Baud, $Seconds 秒 ===" -ForegroundColor Cyan
$buf = New-Object System.Text.StringBuilder
$deadline = (Get-Date).AddSeconds($Seconds)

while ((Get-Date) -lt $deadline) {
    try {
        $chunk = $sp.ReadExisting()
        if ($chunk) { [void]$buf.Append($chunk) }
    } catch { }
    Start-Sleep -Milliseconds 120
}

$sp.Close()
$sp.Dispose()

$text = $buf.ToString()
if (-not $text) {
    Write-Host "(没有收到任何数据)" -ForegroundColor Yellow
    Write-Host "  → 按一下 SW1 复位键，或拔插 USB，再重跑" -ForegroundColor Gray
    exit 0
}

Write-Host $text
Write-Host "`n=== 关键行检查 ===" -ForegroundColor Cyan
foreach ($pat in 'Set DIO2 as RF switch', 'SX126x init result', 'Frequency set', 'Power output set',
                 'Final Tx power', 'Use MCU pin', 'ESP32-S3', 'Region', 'SX126X') {
    $m = [regex]::Matches($text, [regex]::Escape($pat))
    if ($m.Count) { Write-Host "  [找到] $pat  (x$($m.Count))" -ForegroundColor Green }
}
