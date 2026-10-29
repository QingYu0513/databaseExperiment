<#
.SYNOPSIS
    把本项目推送到 GitHub（https://github.com/QingYu0513/databaseExperiment）。

.DESCRIPTION
    本仓库把 miniob/ 当普通目录跟踪，但 miniob/ 自身是一个 git 仓库（clone 自
    oceanbase/miniob，带 5 个 submodule）。只要 miniob/.git 存在，外层 git 就会把
    miniob/ 当作嵌套仓库、只记录一个 gitlink，你的源码改动就推不上去。

    所以本脚本的做法是：
      1. 把 miniob/.git 临时移到 _tmp/miniob_git_backup/（该目录已在 .gitignore 中）
      2. git add -A / git commit
      3. 无论成败，把 miniob/.git 移回原位
      4. git push

.PARAMETER Message
    提交信息，默认「更新」。

.EXAMPLE
    .\push-to-github.ps1                       # 用默认提交信息
    .\push-to-github.ps1 -Message "实验二"      # 指定提交信息
#>
param(
    [string]$Message = "更新"
)

$ErrorActionPreference = 'Stop'

# 脚本在 _setup/ 下，仓库根是它的上一级
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$miniobGit = Join-Path $root 'miniob\.git'
$backupDir = Join-Path $root '_tmp\miniob_git_backup'
$backupGit = Join-Path $backupDir '.git'

Write-Host "仓库根目录: $root" -ForegroundColor Cyan

# 1) 临时移出 miniob/.git
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
$moved = $false
if (Test-Path $miniobGit) {
    if (Test-Path $backupGit) {
        Remove-Item -Recurse -Force $backupGit
    }
    Move-Item $miniobGit $backupGit
    $moved = $true
    Write-Host "已临时移出 miniob\.git（避免被当作嵌套仓库）" -ForegroundColor Yellow
}

try {
    # 2) 提交
    git add -A

    $pending = git status --porcelain
    if ($pending) {
        Write-Host "`n本次改动:" -ForegroundColor Cyan
        $pending | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
        if ($pending.Count -gt 20) { Write-Host "  ... 共 $($pending.Count) 项" }

        git commit -m $Message
        if ($LASTEXITCODE -ne 0) { throw "git commit 失败" }
    }
    else {
        Write-Host "没有需要提交的改动" -ForegroundColor Yellow
    }

    # 3) 推送
    Write-Host "`n开始推送到 origin/main ..." -ForegroundColor Cyan
    git push origin main
    if ($LASTEXITCODE -ne 0) { throw "git push 失败（检查网络与 GitHub 凭据）" }

    Write-Host "`n推送成功" -ForegroundColor Green
    git log -1 --pretty=format:"  %h %s (%an, %ad)" --date=format:"%Y-%m-%d %H:%M"
    Write-Host ""
    git ls-remote origin refs/heads/main
}
finally {
    # 4) 恢复 miniob/.git
    if ($moved -and (Test-Path $backupGit) -and -not (Test-Path $miniobGit)) {
        Move-Item $backupGit $miniobGit
        Write-Host "`n已恢复 miniob\.git" -ForegroundColor Yellow
    }
}
