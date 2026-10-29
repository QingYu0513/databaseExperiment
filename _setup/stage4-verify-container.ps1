# Stage 4: verify that MiniOB actually compiles AND runs inside the container.
# Run as the NORMAL user, AFTER stage3 has pulled the image.
#
# 注意：镜像的 ENTRYPOINT 是 shell 形式的 starter.sh（最后 tail -f /dev/null），
# 所以必须用 --entrypoint 覆盖，否则你传的命令会被完全忽略。
$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'setup_stage4.log'
"" | Out-File -FilePath $log -Encoding utf8
function Say($m) { $m | Tee-Object -FilePath $log -Append | Out-Null; Write-Host $m }

$workspace = Split-Path $PSScriptRoot -Parent          # C:\...\databaseExperiment
$docker    = 'C:\Program Files\Docker\Docker\resources\bin\docker.exe'
$image     = 'oceanbase/miniob'

Say "=== stage 4 start $(Get-Date -Format s) ==="

if (-not (Test-Path $docker)) {
    $c = Get-Command docker.exe -ErrorAction SilentlyContinue
    if ($c) { $docker = $c.Source } else { Say "docker.exe 找不到"; exit 2 }
}

Say "--- engine check ---"
$v = & $docker version --format '{{.Server.Version}}' 2>&1
Say "server: $v"
if ($LASTEXITCODE -ne 0) { Say "Docker 引擎没起来，先跑 stage3"; exit 3 }

Say "--- image check ---"
Say ((& $docker images $image 2>&1 | Out-String))
& $docker image inspect $image *> $null
if ($LASTEXITCODE -ne 0) { Say "本地没有 $image，先跑 stage3 拉取"; exit 4 }

# 脚本和源码分别挂进去：源码挂到 /workspaces/miniob（和 Dev Containers 一致）
$mountSrc   = "${workspace}\miniob"
$mountSetup = "${workspace}\_setup"
Say "mount src  : $mountSrc  -> /workspaces/miniob"
Say "mount setup: $mountSetup -> /verify"

Say ""
Say "--- running in-container verification（首次编译依赖，可能要 10~25 分钟）---"
$args = @(
    'run', '--rm',
    '--entrypoint', 'bash',
    '-v', "${mountSrc}:/workspaces/miniob",
    '-v', "${mountSetup}:/verify",
    '-w', '/workspaces/miniob',
    $image,
    '-lc', 'bash /verify/verify-in-container.sh /workspaces/miniob'
)
Say ("docker " + ($args -join ' '))
Say ""
& $docker @args 2>&1 | ForEach-Object { Say $_ }
$rc = $LASTEXITCODE
Say ""
Say "=== stage 4 done $(Get-Date -Format s) exit=$rc ==="
if ($rc -eq 0) { Say "[SUCCESS] 容器内已能编译并运行 observer / obclient" }
else { Say "[FAILED] 见上面的输出" }
exit $rc
