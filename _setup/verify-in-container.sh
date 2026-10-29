#!/usr/bin/env bash
# ============================================================================
# 在容器内运行：验证 MiniOB 能否"编译 + 运行 observer / obclient"
# 由 stage4-verify-container.ps1 通过 docker run 调用。
#
# 说明：官方镜像在构建时已经用 THIRD_PARTY_INSTALL_PREFIX=/usr/local 跑过
#       `build.sh init`，依赖(libevent/jsoncpp/googletest/benchmark/replxx)都在
#       /usr/local 下。所以这里直接复用，不重新编译依赖，能省 5~15 分钟。
# ============================================================================
set -uo pipefail

SRC="${1:-/workspaces/miniob}"
FAIL=0
T0=$(date +%s)

cd "$SRC" || { echo "[FATAL] 找不到源码目录: $SRC"; exit 1; }

echo "################ 0. 环境自检 ################"
echo "pwd     : $(pwd)"
echo "kernel  : $(uname -sr)"
echo "gcc     : $(gcc --version 2>/dev/null | head -1)"
echo "g++     : $(g++ --version 2>/dev/null | head -1)"
echo "cmake   : $(cmake --version 2>/dev/null | head -1)"
echo "make    : $(make --version 2>/dev/null | head -1)"
echo "gdb     : $(gdb --version 2>/dev/null | head -1)"
echo "flex    : $(flex --version 2>/dev/null || echo '缺失')"
echo "bison   : $(bison --version 2>/dev/null | head -1)"
echo "nproc   : $(nproc 2>/dev/null)"
command -v python3 >/dev/null 2>&1 && echo "python3 : $(python3 --version)" || echo "python3 : 未安装"

echo
echo "################ 1. git 信任目录 ################"
# 挂载来的 Windows 目录属主和容器内 root 不一致会触发 git 的
# "detected dubious ownership"。而 build.sh init 第一行是
# `git submodule update --init || return`，git 一失败就会**静默跳过依赖编译**。
git config --global --add safe.directory '*' 2>/dev/null || true
echo "HEAD: $(git rev-parse --short HEAD 2>&1)"
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "[ OK ] git 可用"
else
  echo "[FAIL] git 不可用"; FAIL=1
fi

echo
echo "################ 2. 第三方依赖是否已就绪 ################"
if [ -f /usr/local/lib/libevent.a ] || [ -f /usr/local/lib64/libevent.a ]; then
  echo "[ OK ] 官方镜像预置依赖存在（/usr/local），复用它们，跳过 build.sh init"
  export THIRD_PARTY_INSTALL_PREFIX=/usr/local
else
  echo "[ .. ] /usr/local 没有预置依赖，改为跑 build.sh init（较慢）"
  bash build.sh init > /tmp/init.log 2>&1
  echo "build.sh init exit=$?"
  tail -15 /tmp/init.log
fi
echo "THIRD_PARTY_INSTALL_PREFIX=${THIRD_PARTY_INSTALL_PREFIX:-<default>}"

echo
echo "################ 3. 编译 MiniOB (debug) ################"
echo "开始编译，请稍候（源码在 Windows 挂载盘上，会比容器内磁盘慢一些）..."
BSTART=$(date +%s)
bash build.sh debug > /tmp/build.log 2>&1
BRC=$?
BEND=$(date +%s)
echo "build.sh debug exit=$BRC   耗时 $((BEND-BSTART)) 秒"
tail -25 /tmp/build.log

echo
echo "################ 4. 产物检查 ################"
for b in build_debug/bin/observer build_debug/bin/obclient; do
  if [ -x "$b" ]; then
    echo "[ OK ] $b  ($(stat -c%s "$b") bytes)"
  else
    echo "[FAIL] $b 不存在或不可执行"
    FAIL=1
  fi
done
ls -l build_debug/bin/ 2>/dev/null

echo
echo "################ 5. 冒烟测试：observer 跑 SQL ################"
# -P cli 走 CliServer（stdin/stdout），不需要 python3 也不需要开端口
cat > /tmp/smoke.sql <<'SQL'
create table t1(id int, name char(20));
insert into t1 values(1,'aaa');
insert into t1 values(2,'bbb');
select * from t1;
SQL

set +e
timeout 90 ./build_debug/bin/observer -f etc/observer.ini -P cli < /tmp/smoke.sql > /tmp/smoke.out 2>&1
RC=$?
set -e
echo "observer exit=$RC （124=超时未退出，服务端一直在等输入，通常正常）"
echo "---------------- observer 输出 ----------------"
cat /tmp/smoke.out
echo "----------------------------------------------"

if grep -q 'aaa' /tmp/smoke.out && grep -q 'bbb' /tmp/smoke.out; then
  echo "[PASS] observer 正常运行，create/insert/select 都返回了预期数据"
else
  echo "[WARN] 输出里没有 'aaa'/'bbb'，请人工确认上面的输出"
  FAIL=1
fi

T1=$(date +%s)
echo
echo "################ 结果（总耗时 $((T1-T0)) 秒）################"
if [ "$FAIL" -eq 0 ]; then
  echo "[SUCCESS] 容器内可以编译并运行 observer / obclient"
else
  echo "[FAILED] 有检查项未通过，见上面的 [FAIL]/[WARN]"
fi
exit $FAIL
