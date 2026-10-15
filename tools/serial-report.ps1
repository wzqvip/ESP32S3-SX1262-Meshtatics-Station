# 抓串口日志并分类统计（避免刷屏）
param(
    [string] $Port = 'COM48',
    [int]    $Baud = 115200,
    [int]    $Seconds = 14
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 300
try { $sp.Open() } catch { Write-Host "打开失败: $($_.Exception.Message)" -ForegroundColor Red; exit 1 }

$buf = New-Object System.Text.StringBuilder
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    try { $ch = $sp.ReadExisting(); if ($ch) { [void]$buf.Append($ch) } } catch { }
    Start-Sleep -Milliseconds 100
}
$sp.Close(); $sp.Dispose()

$text = $buf.ToString()
Write-Host "=== 收到 $($text.Length) 字符 ===" -ForegroundColor Cyan

if (-not $text.Trim()) { Write-Host '(无数据)' -ForegroundColor Yellow; exit 0 }

# 分类统计
$checks = [ordered]@{
    'psiram/psram 报错'   = 'psram'
    'Guru Meditation'     = 'Guru Meditation'
    'Rebooting'           = 'Rebooting'
    'DIO2/RF switch'      = 'DIO2'
    'SX126x init'         = 'SX126x init'
    'Final Tx power'      = 'Final Tx power'
    'Meshtastic 启动'      = 'Meshtastic'
    'Region/区域'          = 'Region'
    'Powered by'          = 'Powered by'
    'setup()/loop 崩溃'    = 'setup()'
}
Write-Host "`n=== 分类统计 ===" -ForegroundColor Cyan
foreach ($k in $checks.Keys) {
    $n = ([regex]::Matches($text, [regex]::Escape($checks[$k]))).Count
    $color = if ($n -eq 0) { 'DarkGray' } elseif ($k -match 'psram|Guru|Reboot') { 'Red' } else { 'Green' }
    Write-Host ("  {0,-20} {1}" -f $k, $n) -ForegroundColor $color
}

# 打印去重后的独特行（截取，避免刷屏）
Write-Host "`n=== 去重后的独特内容行 (最多 45 条) ===" -ForegroundColor Cyan
$lines = $text -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' }
$seen = @{}
$count = 0
foreach ($l in $lines) {
    $key = $l -replace '[0-9]', '#'
    if (-not $seen.ContainsKey($key)) {
        $seen[$key] = 1
        Write-Host "  $l"
        $count++
        if ($count -ge 45) { break }
    }
}
