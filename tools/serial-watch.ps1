# 持续监听串口，实时打印（带时间戳），用于"你按复位我来抓"
param(
    [string] $Port = 'COM48',
    [int]    $Baud = 115200,
    [int]    $Seconds = 90
)

Write-Host "=== 持续监听 $Port @ $Baud ，$Seconds 秒 ===" -ForegroundColor Cyan
Write-Host ">>> 请在这段时间内按一下板子的 SW1 复位键，或拔插一次 Pico 的 USB-C <<<`n" -ForegroundColor Yellow

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 300
try {
    $sp.Open()
} catch {
    Write-Host "打开失败: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$total = 0
$deadline = (Get-Date).AddSeconds($Seconds)
$lastBeat = Get-Date

while ((Get-Date) -lt $deadline) {
    try {
        $chunk = $sp.ReadExisting()
        if ($chunk) {
            $total += $chunk.Length
            # 不换行输出，保持原始格式
            Write-Host $chunk -NoNewline
        }
    } catch { }

    # 每 15 秒报一次心跳，证明还在监听
    if (((Get-Date) - $lastBeat).TotalSeconds -ge 15) {
        $left = [int]($deadline - (Get-Date)).TotalSeconds
        if ($left -gt 0) { Write-Host "`n[...还在监听，剩余 ${left}s，已收 $total 字符...]" -ForegroundColor DarkGray }
        $lastBeat = Get-Date
    }
    Start-Sleep -Milliseconds 100
}

$sp.Close(); $sp.Dispose()
Write-Host "`n`n=== 监听结束，共收到 $total 字符 ===" -ForegroundColor Cyan
