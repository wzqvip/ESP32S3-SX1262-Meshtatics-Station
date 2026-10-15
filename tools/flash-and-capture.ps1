# 烧录后立刻复位并抓串口 (避免复位与监听之间的时间差)
param(
    [string] $Port   = 'COM48',
    [string] $Image  = 'hw-selftest\.pio\build\selftest\firmware.factory.bin',
    [int]    $Offset = 0,
    [int]    $Baud   = 115200,
    [int]    $Seconds = 18,
    [switch] $SkipFlash
)

$ErrorActionPreference = 'Stop'
$esptool = 'D:\meshtastic-build\pio-core\penv\Scripts\esptool.exe'
$env:PYTHONIOENCODING = 'utf-8'

if (-not $SkipFlash) {
    Write-Host "=== 烧录 $Image ===" -ForegroundColor Cyan
    & $esptool --chip esp32s3 --port $Port --after no-reset write-flash $Offset $Image 2>&1 |
        Select-String 'Hash of data verified|Wrote \d+ bytes|ERROR' | ForEach-Object { "  " + $_.Line.Trim() }
}

# 立刻打开串口 (先开好, 再触发复位, 避免错过开头)
Write-Host "`n=== 先打开 $Port, 然后触发复位 ===" -ForegroundColor Cyan
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 300
$sp.Open()
Write-Host "  串口已打开" -ForegroundColor DarkGray

# 触发复位: 用 esptool 的 hard-reset (会短暂占用端口, 所以先关掉)
$sp.Close()
& $esptool --chip esp32s3 --port $Port --before default-reset --after hard-reset run 2>&1 |
    Select-String 'Hard resetting|ERROR' | ForEach-Object { "  " + $_.Line.Trim() }

# 复位后立刻重新打开
Start-Sleep -Milliseconds 700
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 300
try { $sp.Open() } catch { Write-Host "重开失败: $($_.Exception.Message)" -ForegroundColor Red; exit 1 }
Write-Host "  已重新打开, 开始采集 $Seconds 秒...`n" -ForegroundColor DarkGray

$buf = New-Object System.Text.StringBuilder
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    try { $ch = $sp.ReadExisting(); if ($ch) { [void]$buf.Append($ch) } } catch { }
    Start-Sleep -Milliseconds 80
}
$sp.Close(); $sp.Dispose()

$t = $buf.ToString()
Write-Host "=== 收到 $($t.Length) 字符 ===" -ForegroundColor Cyan
if ($t.Trim()) { Write-Host $t } else { Write-Host '(无数据)' -ForegroundColor Yellow }
