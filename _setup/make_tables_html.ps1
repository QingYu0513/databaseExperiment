# 生成"表数据.html"：把所有表导出成一个网页，然后用默认浏览器打开。
# 双击 view-tables.cmd 即可运行。
$ErrorActionPreference = 'Continue'

# 容器里 bash 输出的是 UTF-8，而 PowerShell 5.1 默认按 GBK(ANSI) 解码，
# 不设这两行的话页面里的中文会变成乱码。
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch {}
try { $OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch {}

$ws     = Split-Path $PSScriptRoot -Parent
$docker = 'C:\Program Files\Docker\Docker\resources\bin\docker.exe'
$out    = Join-Path $ws '表数据.html'
$log    = Join-Path $PSScriptRoot 'view_tables.log'

function Say($m) { $m | Tee-Object -FilePath $log -Append | Out-Null; Write-Host $m }
"" | Out-File -FilePath $log -Encoding utf8

Say "=== 生成表页面 $(Get-Date -Format s) ==="

if (-not (Test-Path $docker)) {
    $c = Get-Command docker.exe -ErrorAction SilentlyContinue
    if ($c) { $docker = $c.Source } else { Say "找不到 docker.exe"; exit 1 }
}

# 1) 确保容器在运行
$name = (& $docker ps --filter 'name=miniob-gui' --format '{{.Names}}' 2>$null) -join ''
if (-not $name) {
    Say "容器 miniob-gui 没在运行，尝试启动..."
    & $docker start miniob-gui 2>&1 | Out-Null
    Start-Sleep -Seconds 6
    $name = (& $docker ps --filter 'name=miniob-gui' --format '{{.Names}}' 2>$null) -join ''
}
if (-not $name) { Say "启动失败：miniob-gui 不在运行"; exit 2 }
Say "容器: $name"

# 2) 确保容器里有 mysql 客户端（容器被重建后会丢）
& $docker exec miniob-gui sh -c "command -v mysql" *> $null
if ($LASTEXITCODE -ne 0) {
    Say "容器里没有 mysql 客户端，正在安装（约 30 秒）..."
    & $docker exec miniob-gui bash -c "apt-get update -qq >/dev/null 2>&1; apt-get install -y -qq --no-install-recommends mariadb-client >/dev/null 2>&1; command -v mysql" 2>&1 | Out-Null
}
Say "mysql 客户端: " + ((& $docker exec miniob-gui sh -c "command -v mysql" 2>&1) -join '')

# 3) 把生成脚本拷进去并执行
& $docker cp (Join-Path $PSScriptRoot 'make_html.sh') miniob-gui:/tmp/make_html.sh 2>&1 | Out-Null
$html = (& $docker exec miniob-gui bash /tmp/make_html.sh 2>$null) -join "`r`n"

if (-not $html -or $html.Length -lt 200) {
    Say "生成失败，输出为空。请确认 observer 在跑。"
    Say $html
    exit 3
}

[System.IO.File]::WriteAllText($out, $html, (New-Object System.Text.UTF8Encoding($false)))
Say "已生成: $out ($($html.Length) 字符)"

# 4) 打开浏览器
Start-Process $out
Say "=== 完成 ==="
