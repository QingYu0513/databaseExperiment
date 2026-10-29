#!/bin/bash
# 在容器内运行：把所有表导出成一个 HTML 页面（浏览器里看就是表格界面）。
# 由 查看表.cmd 调用。
MYSQL="mysql -h 127.0.0.1 -P 6789 -u root --skip-ssl"

echo '<!DOCTYPE html>'
echo '<html><head><meta charset="utf-8"><title>MiniOB 数据表</title>'
echo '<style>'
echo 'body{font-family:"Segoe UI",system-ui,sans-serif;margin:26px;background:#fafafa;color:#222}'
echo 'h1{margin:0 0 4px;font-size:22px}'
echo 'h2{margin:26px 0 6px;font-size:17px;color:#1a4f8a;border-left:4px solid #1a4f8a;padding-left:9px}'
echo 'table{border-collapse:collapse;margin:6px 0 4px;background:#fff}'
echo 'td,th{border:1px solid #c8c8c8;padding:5px 12px;font-size:14px}'
echo 'th{background:#eef3f8;text-align:left}'
echo 'tr:nth-child(even) td{background:#fbfdff}'
echo '.meta{color:#666;font-size:13px}'
echo '.cols{color:#888;font-size:12px;margin:2px 0 0}'
echo '</style></head><body>'
echo '<h1>MiniOB 数据表</h1>'
echo "<p class=\"meta\">连接：127.0.0.1:13306 &nbsp;|&nbsp; 用户 root &nbsp;|&nbsp; 无密码 &nbsp;|&nbsp; 生成时间 $(date '+%Y-%m-%d %H:%M:%S')</p>"

TABLES=$($MYSQL -N -e "show tables;" 2>/dev/null | tr -d '\r')

if [ -z "$TABLES" ]; then
  echo '<p>数据库里还没有表。</p>'
fi

for t in $TABLES; do
  echo "<h2>${t}</h2>"
  $MYSQL --html -e "select * from ${t};" 2>&1
  echo "<div class=\"cols\">列结构：</div>"
  $MYSQL --html -e "desc ${t};" 2>&1
done

echo '</body></html>'
