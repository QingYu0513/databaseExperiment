# 由 run-apply-mirror.cmd 通过 explorer.exe 启动，**运行在 DSH 沙箱之外**（普通用户身份）。
# 这样 docker CLI 才能连上 Docker Desktop 的命名管道，也能写用户目录下的配置文件。
$ErrorActionPreference = 'Continue'
$ws  = 'C:\Users\24235\Desktop\databaseExperiment'
$log = Join-Path $ws '_setup\apply_mirror.log'
"" | Out-File -FilePath $log -Encoding utf8
function Say($m) { $m | Out-File -FilePath $log -Encoding utf8 -Append }

$docker = 'C:\Program Files\Docker\Docker\resources\bin\docker.exe'
$MIRROR = 'https://docker.m.daocloud.io'

Say "=== apply-mirror start $(Get-Date -Format s) ==="
Say "whoami: $(whoami)"

# ---------- 1) 把 registry-mirrors 写进真正生效的 daemon.json ----------
$daemonPath = Join-Path $env:USERPROFILE '.docker\daemon.json'
Say "daemon.json: $daemonPath"
if (Test-Path $daemonPath) {
    Copy-Item $daemonPath "$daemonPath.bak" -Force
    Say "--- old ---"; Say (Get-Content $daemonPath -Raw)
}
$newDaemon = @'
{
  "builder": {
    "gc": {
      "defaultKeepStorage": "20GB",
      "enabled": true
    }
  },
  "experimental": false,
  "registry-mirrors": ["https://docker.m.daocloud.io"]
}
'@
New-Item -ItemType Directory -Force -Path (Split-Path $daemonPath) | Out-Null
$newDaemon | Out-File -FilePath $daemonPath -Encoding ascii
Say "--- new ---"; Say (Get-Content $daemonPath -Raw)

# ---------- 2) 清掉 settings-store.json 里放错位置的键，并打开开机自启 ----------
$store = Join-Path $env:APPDATA 'Docker\settings-store.json'
if (Test-Path "$store.bak") {
    Copy-Item "$store.bak" $store -Force
    Say "restored settings-store.json from .bak (removed misplaced RegistryMirrors)"
}
try {
    $j = Get-Content $store -Raw -Encoding utf8 | ConvertFrom-Json
    $j | Add-Member -NotePropertyName 'AutoStart' -NotePropertyValue $true -Force
    $j | ConvertTo-Json -Depth 40 | Set-Content $store -Encoding utf8
    Say "set AutoStart=true (Docker Desktop 会在登录时自动启动)"
} catch { Say "settings-store patch failed: $($_.Exception.Message)" }

# ---------- 3) 重启引擎让镜像加速生效 ----------
Say "--- restart engine ---"
Say ((& $docker desktop restart 2>&1 | Out-String).Trim())
for ($i=1; $i -le 120; $i++) {
    $v = & $docker version --format '{{.Server.Version}}' 2>$null
    if ($LASTEXITCODE -eq 0 -and $v) { Say "engine up: $v"; break }
    Start-Sleep -Seconds 5
}

Say "--- docker info: mirrors ---"
$info = & $docker info 2>&1 | Out-String
($info -split "`r?`n") | Where-Object { $_ -match 'Registry Mirrors|^\s+https?://' } | ForEach-Object { Say $_ }

# ---------- 4) 通过镜像源拉 miniob 并重打成官方名字 ----------
Say ""
Say "--- pulling oceanbase/miniob via mirror (369 MB, 10~20 min) ---"
& $docker pull "$MIRROR/oceanbase/miniob:latest" 2>&1 | ForEach-Object { Say $_ }
Say "pull exit=$LASTEXITCODE"

& $docker tag "$MIRROR/oceanbase/miniob:latest" 'oceanbase/miniob:latest' 2>&1 | ForEach-Object { Say $_ }
Say "tag exit=$LASTEXITCODE"

# ---------- 5) 冒烟测试 ----------
Say ""
Say "--- images ---"
(& $docker images 2>&1) | ForEach-Object { Say $_ }

Say "--- smoke: toolchain inside the image ---"
# 注意：镜像 ENTRYPOINT 是 shell 形式的 starter.sh，必须覆盖 entrypoint，否则命令会被忽略
(& $docker run --rm --entrypoint bash 'oceanbase/miniob' -lc 'gcc --version | head -1; cmake --version | head -1; gdb --version | head -1; flex --version; bison --version | head -1; echo OK-TOOLCHAIN' 2>&1) | ForEach-Object { Say $_ }

Say "=== apply-mirror done $(Get-Date -Format s) ==="
