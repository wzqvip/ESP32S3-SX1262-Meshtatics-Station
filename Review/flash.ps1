<#
.SYNOPSIS
  烧录 T-mesh (ESP32-S3 N8R2) 固件。

.DESCRIPTION
  自动定位 esptool 与串口，按需整片擦除后写入镜像。

  重要：必须插【Pico 模块自己的 USB-C】。载板上的 USB-C 是 PD 诱骗取电用的，
        没有数据线，插它电脑上不会出现串口。

.EXAMPLE
  .\flash.ps1                      # 自动找端口, 只更新应用 (保留配置)
  .\flash.ps1 -Mode factory        # 烧合并镜像 (从 0x0)
  .\flash.ps1 -Erase               # 整片擦除 + 烧 factory
  .\flash.ps1 -Port COM5           # 指定端口
  .\flash.ps1 -AutoReset 0         # 手动进下载模式时用 (配合按住 SW2 上电)

.NOTES
  -Erase 用于分区表发生变化的情况 (例如从 16MB 布局换到 8MB 布局)。
  不擦除会让已保存的 Meshtastic 配置从错误地址读取。
#>
[CmdletBinding()]
param(
    [string]   $Port,
    [ValidateSet('factory', 'app', 'littlefs')]
    [string]   $Mode = 'app',
    [int]      $Baud = 921600,
    [switch]   $Erase,
    [string]   $FirmwareDir = "$PSScriptRoot\firmware"
)

$ErrorActionPreference = 'Stop'

function Write-Head($t) { Write-Host "`n=== $t ===" -ForegroundColor Cyan }
function Write-Ok($t)   { Write-Host "  [OK] $t"   -ForegroundColor Green }
function Write-Warn2($t){ Write-Host "  [!]  $t"   -ForegroundColor Yellow }
function Write-Err($t)  { Write-Host "  [X]  $t"   -ForegroundColor Red }

# ---------------------------------------------------------------- 镜像文件
# 偏移取自 firmware 构建实际使用的 default_8MB.csv:
#   nvs 0x9000 / otadata 0xe000 / app0 0x10000 (0x330000)
#   app1 0x340000 / spiffs 0x670000 (0x180000) / coredump 0x7F0000
$files = @{
    factory  = @{ Name = 'tmesh-s3-e22-factory.bin';  Offset = '0x0'     }
    app      = @{ Name = 'tmesh-s3-e22-app.bin';      Offset = '0x10000' }
    littlefs = @{ Name = 'tmesh-s3-e22-littlefs.bin'; Offset = '0x670000' }
}

Write-Head '镜像'
$img = Join-Path $FirmwareDir $files[$Mode].Name
if (-not (Test-Path $img)) {
    Write-Err "找不到 $img"
    Write-Host "  可用的镜像:" -ForegroundColor Gray
    if (Test-Path $FirmwareDir) {
        Get-ChildItem $FirmwareDir -Filter *.bin | ForEach-Object { Write-Host "    $($_.Name)" -ForegroundColor Gray }
    } else {
        Write-Host "    目录不存在: $FirmwareDir" -ForegroundColor Gray
    }
    exit 1
}
$imgSizeMB = [math]::Round((Get-Item $img).Length / 1MB, 2)
Write-Ok "$($files[$Mode].Name)  ($imgSizeMB MB)  ->  写入偏移 $($files[$Mode].Offset)"

# ---------------------------------------------------------------- esptool
Write-Head 'esptool'

# 按优先级探测，每一步都【实际验证】能用，不能只看文件存在
#   1. PLATFORMIO_CORE_DIR 指向的 penv（推荐，工具链都在这）
#   2. 默认 PlatformIO (~/.platformio) 的 penv
#   3. 常见自建路径
#   4. 系统 python / py
$esptoolExe = $null   # 可直接调用的 esptool 可执行文件
$pyForMod   = $null   # 用 -m esptool 调用的解释器

function Test-Module([string]$exe) {
    if (-not (Test-Path $exe)) { return $false }
    & $exe -c "import esptool" 2>$null | Out-Null
    return ($LASTEXITCODE -eq 0)
}
function Test-Exe([string]$exe) {
    if (-not (Test-Path $exe)) { return $false }
    & $exe version 2>$null | Out-Null
    return ($LASTEXITCODE -eq 0)
}

$pioCores = @()
if ($env:PLATFORMIO_CORE_DIR) { $pioCores += $env:PLATFORMIO_CORE_DIR }
$pioCores += "$env:USERPROFILE\.platformio"
$pioCores += 'D:\meshtastic-build\pio-core'

foreach ($core in $pioCores) {
    $exe = Join-Path $core 'penv\Scripts\esptool.exe'
    $py  = Join-Path $core 'penv\Scripts\python.exe'
    if (Test-Exe $exe) { $esptoolExe = $exe; break }
    if (Test-Module $py) { $pyForMod = $py; break }
}

if (-not $esptoolExe -and -not $pyForMod) {
    foreach ($name in 'python', 'py') {
        $c = Get-Command $name -ErrorAction SilentlyContinue
        if ($c -and (Test-Module $c.Source)) { $pyForMod = $c.Source; break }
    }
}

if (-not $esptoolExe -and -not $pyForMod) {
    Write-Err '找不到可用的 esptool'
    Write-Host '  三种办法任选:' -ForegroundColor Gray
    Write-Host '    ① 指定 PlatformIO 位置:  $env:PLATFORMIO_CORE_DIR="D:\meshtastic-build\pio-core"' -ForegroundColor Gray
    Write-Host '    ② 装 PlatformIO (会自带 esptool)' -ForegroundColor Gray
    Write-Host '    ③ python -m pip install esptool' -ForegroundColor Gray
    exit 1
}

if ($esptoolExe) { Write-Ok "esptool: $esptoolExe" } else { Write-Ok "esptool (python -m): $pyForMod" }

# 统一调用入口
# ★ 必须把 stdout/stderr 重定向到 Information/Error 流, 不能让它进入返回值。
#   否则 `& $exe @args` 的输出会被函数当成返回值收集, return 出的是
#   「输出行 + 退出码」组成的数组, 后面的 `$rc -ne 0` 就恒为真 ——
#   表现为"命令明明成功却报失败", 而且真实输出被吞掉, 极难排查。
function Invoke-Esptool {
    param([string[]] $EsptoolArgs)
    # ★ 强制 UTF-8: esptool v5 用 ░▒▓ 等 Unicode 字符画进度条,
    #   中文 Windows 控制台默认 GBK, 会抛
    #   UnicodeEncodeError: 'gbk' codec can't encode character '\u2591'
    #   表现为烧录中途崩掉 (其实擦除已经完成)。
    $prevEnc = $env:PYTHONIOENCODING
    $env:PYTHONIOENCODING = 'utf-8'
    try {
        if ($esptoolExe) {
            & $esptoolExe @EsptoolArgs 2>&1 | Write-Host
        } else {
            & $pyForMod -m esptool @EsptoolArgs 2>&1 | Write-Host
        }
        return $LASTEXITCODE
    } finally {
        $env:PYTHONIOENCODING = $prevEnc
    }
}

# ---------------------------------------------------------------- 串口
Write-Head '串口'
if (-not $Port) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    if ($ports.Count -eq 0) {
        Write-Err '没有检测到任何串口'
        Write-Host '  检查: ① 是否插在 Pico 模块自己的 USB-C 上 (不是载板那个)' -ForegroundColor Gray
        Write-Host '        ② USB 线是否支持数据传输' -ForegroundColor Gray
        Write-Host '        ③ 是否装了 CH343 驱动 (WCH CH34x)' -ForegroundColor Gray
        exit 1
    }
    if ($ports.Count -eq 1) {
        $Port = $ports[0]
        Write-Ok "自动选中 $Port"
    } else {
        Write-Warn2 "检测到多个串口, 无法确定用哪个:"
        foreach ($p in $ports) {
            $dev = (Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -like "*($p)*" }).Name
            Write-Host "    $p  $dev" -ForegroundColor Gray
        }
        Write-Host "`n  请用 -Port 指定, 例如:  .\flash.ps1 -Port $($ports[0])" -ForegroundColor Yellow
        exit 1
    }
} else {
    Write-Ok "使用指定端口 $Port"
}

# 提示设备名 (仅信息性: 本板也可能枚举成原生 USB-Serial/JTAG 而非 CH343)
$devName = (Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "*($Port)*" }).Name
if ($devName) {
    Write-Host "  设备: $devName" -ForegroundColor Gray
}

# ---------------------------------------------------------------- 擦除
# 注意: 必须加 --after no-reset。
# 本板不通过 CH343P 下载, 而是走 ESP32-S3 原生 USB-Serial/JTAG (COM48)。
# 该模式下【没有 RTS 线】, esptool 收尾的 "Hard resetting via RTS pin" 会失败并
# 抛 PermissionError(13), 然后返回非 0 —— 但擦除/烧录其实已经完成了。
# esptool v5 把这个复位失败当致命错误, 所以用 --after no-reset 绕开,
# 改在最后用烧录器复位序列单独复一次。
$common = @('--chip', 'esp32s3', '--port', $Port, '--after', 'no-reset')

if ($Erase) {
    Write-Head '整片擦除'
    Write-Warn2 '这会清除所有已保存的配置 (节点名/频道/密钥)'
    $rc = Invoke-Esptool ($common + @('erase-flash'))
    if ($rc -ne 0) { Write-Err '擦除失败'; exit 1 }
    Write-Ok '擦除完成'
}

# ---------------------------------------------------------------- 烧录
Write-Head '烧录'
if ($Erase -and $Mode -ne 'factory') {
    Write-Warn2 "刚擦除过, 只烧 $Mode 是起不来的 —— 建议改用 -Mode factory"
}

$rc = Invoke-Esptool ($common + @(
    '--baud', $Baud,
    'write-flash', $files[$Mode].Offset, $img
))

if ($rc -ne 0) {
    Write-Err '烧录失败'
    Write-Host "`n  如果报 Failed to connect:" -ForegroundColor Yellow
    Write-Host '    手动进下载模式: 按住 SW2(BOOT) -> 点一下 SW1(RESET) -> 松开 SW1 -> 松开 SW2' -ForegroundColor Gray
    Write-Host '    然后降低波特率重试:  -Baud 115200' -ForegroundColor Gray
    exit 1
}
Write-Ok '烧录完成'

# ---------------------------------------------------------------- 复位启动
Write-Head '复位启动'
$rc = Invoke-Esptool @('--chip', 'esp32s3', '--port', $Port, '--before', 'default-reset', '--after', 'hard-reset', 'run')
if ($rc -eq 0) {
    Write-Ok '已触发复位，板子开始运行新固件'
} else {
    Write-Warn2 '自动复位没成功（USB-Serial/JTAG 下常见）'
    Write-Host '  → 手动复位: 按一下 SW1，或拔插一次 USB' -ForegroundColor Gray
}

Write-Host @"

下一步: 接串口看日志 (115200), 确认这一行出现:
    Set DIO2 as RF switch, result: 0

详细验证顺序见 Review\MESHTASTIC-VARIANT.md 第 5 节。
"@ -ForegroundColor Gray
