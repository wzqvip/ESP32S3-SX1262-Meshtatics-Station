# 扫描多个串口，找哪个在输出数据（只读）
param(
    [string[]] $Ports,
    [int]      $Baud = 115200,
    [int]      $Seconds = 12
)

if (-not $Ports) { $Ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object }

# port -> @{ Sp = SerialPort; Buf = StringBuilder }
$open = [ordered]@{}

foreach ($p in $Ports) {
    try {
        $sp = New-Object System.IO.Ports.SerialPort $p, $Baud, 'None', 8, 'One'
        $sp.ReadTimeout  = 200
        $sp.WriteTimeout = 200
        $sp.DtrEnable = $true
        $sp.RtsEnable = $false
        $sp.Open()
        $open[$p] = @{ Sp = $sp; Buf = (New-Object System.Text.StringBuilder) }
        Write-Host "  [打开] $p" -ForegroundColor DarkGray
    } catch {
        Write-Host "  [跳过] $p : $($_.Exception.Message)" -ForegroundColor DarkYellow
    }
}

if ($open.Count -eq 0) { Write-Host '没有任何串口可用' -ForegroundColor Red; exit 1 }

Write-Host "`n=== 监听中 ($Seconds 秒) —— 请现在按一下 SW1 复位键，或拔插 USB ===" -ForegroundColor Cyan

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    foreach ($p in $open.Keys) {
        try {
            $chunk = $open[$p].Sp.ReadExisting()
            if ($chunk) { [void]$open[$p].Buf.Append($chunk) }
        } catch { }
    }
    Start-Sleep -Milliseconds 150
}

Write-Host "`n=== 结果 ===" -ForegroundColor Cyan
foreach ($p in $open.Keys) {
    $t = $open[$p].Buf.ToString()
    $open[$p].Sp.Close()
    $open[$p].Sp.Dispose()
    if ($t.Trim()) {
        Write-Host "`n--- $p : 收到 $($t.Length) 字符 ---" -ForegroundColor Green
        Write-Host $t
    } else {
        Write-Host "  $p : 无数据" -ForegroundColor DarkGray
    }
}
