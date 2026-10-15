# 用不同的 DTR/RTS 组合尝试读取串口（只读，用于排查）
param(
    [string] $Port = 'COM48',
    [int]    $Baud = 115200,
    [int]    $Seconds = 6
)

$combos = @(
    @{ D = $false; R = $false; Name = 'DTR=0 RTS=0 (默认, 让驱动处理)' },
    @{ D = $true;  R = $false; Name = 'DTR=1 RTS=0 (之前用的)' },
    @{ D = $false; R = $true;  Name = 'DTR=0 RTS=1' },
    @{ D = $true;  R = $true;  Name = 'DTR=1 RTS=1' }
)

foreach ($c in $combos) {
    Write-Host "`n--- 尝试 $($c.Name) ---" -ForegroundColor Cyan
    $sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
    $sp.ReadTimeout = 300
    try {
        $sp.DtrEnable = $c.D
        $sp.RtsEnable = $c.R
        $sp.Open()
        # 打开后再设一次（有些驱动在 Open 时会重置）
        $sp.DtrEnable = $c.D
        $sp.RtsEnable = $c.R
        if ($c.D) { $sp.DtrEnable = $true }
        Start-Sleep -Milliseconds 100

        $buf = New-Object System.Text.StringBuilder
        $deadline = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $deadline) {
            try { $ch = $sp.ReadExisting(); if ($ch) { [void]$buf.Append($ch) } } catch { }
            Start-Sleep -Milliseconds 100
        }
        $t = $buf.ToString()
        $sp.Close(); $sp.Dispose()

        if ($t.Trim()) {
            Write-Host "  ★ 收到 $($t.Length) 字符:" -ForegroundColor Green
            Write-Host $t
        } else {
            Write-Host '  无数据' -ForegroundColor DarkGray
        }
    } catch {
        Write-Host "  打开失败: $($_.Exception.Message)" -ForegroundColor Yellow
        try { $sp.Dispose() } catch { }
    }
}
