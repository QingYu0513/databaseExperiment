# 由 run-pull.cmd 通过 explorer.exe 启动，运行在 DSH 沙箱之外（普通用户）。
# 现在 daemon 级镜像加速已经生效（docker info 里能看到 Registry Mirrors），
# 所以直接 pull 官方名字就行 —— 这正是实验指导书里的命令形式。
$ErrorActionPreference = 'Continue'
$ws  = 'C:\Users\24235\Desktop\databaseExperiment'
$log = Join-Path $ws '_setup\pull_miniob.log'
"" | Out-File -FilePath $log -Encoding utf8
function Say($m) { $m | Out-File -FilePath $log -Encoding utf8 -Append }

$docker = 'C:\Program Files\Docker\Docker\resources\bin\docker.exe'
$HOST_MIRROR = 'docker.m.daocloud.io'   # 注意：镜像引用里不能带 https:// 前缀

Say "=== pull-miniob start $(Get-Date -Format s) ==="

Say "--- confirm mirror is active ---"
$info = & $docker info 2>&1 | Out-String
($info -split "`r?`n") | Where-Object { $_ -match 'Registry Mirrors|https://docker' } | ForEach-Object { Say $_ }

# ---------- 1) 用官方名字直接拉（走已生效的镜像加速）----------
Say ""
Say "--- [1] docker pull oceanbase/miniob:latest  (via active mirror; 369 MB) ---"
(& $docker pull 'oceanbase/miniob:latest' 2>&1) | ForEach-Object { Say $_ }
$rc1 = $LASTEXITCODE
Say "pull exit=$rc1"

# ---------- 2) 失败则显式走镜像源主机再重打标签 ----------
if ($rc1 -ne 0) {
    Say ""
    Say "--- [2] fallback: pull via explicit mirror host then retag ---"
    (& $docker pull "$HOST_MIRROR/oceanbase/miniob:latest" 2>&1) | ForEach-Object { Say $_ }
    Say "pull via host exit=$LASTEXITCODE"
    (& $docker tag "$HOST_MIRROR/oceanbase/miniob:latest" 'oceanbase/miniob:latest' 2>&1) | ForEach-Object { Say $_ }
    Say "tag exit=$LASTEXITCODE"
}

Say ""
Say "--- images ---"
(& $docker images 2>&1) | ForEach-Object { Say $_ }

# ---------- 3) 冒烟测试：容器内工具链 ----------
Say ""
Say "--- smoke test: toolchain inside oceanbase/miniob ---"
# ENTRYPOINT 是 shell 形式的 starter.sh（最后 tail -f /dev/null），
# 不覆盖 entrypoint 的话，传进去的命令会被完全忽略。
(& $docker run --rm --entrypoint bash 'oceanbase/miniob' -lc 'echo "--- os ---"; head -1 /etc/os-release; echo "--- gcc ---"; gcc --version | head -1; echo "--- cmake ---"; cmake --version | head -1; echo "--- gdb ---"; gdb --version | head -1; echo "--- flex ---"; flex --version; echo "--- bison ---"; bison --version | head -1; echo TOOLCHAIN-OK' 2>&1) | ForEach-Object { Say $_ }

Say "=== pull-miniob done $(Get-Date -Format s) ==="
