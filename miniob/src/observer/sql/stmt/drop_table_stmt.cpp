/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/stmt/drop_table_stmt.h"
#include "common/log/log.h"
#include "storage/db/db.h"

/**
 * @brief DROP TABLE 的语义分析阶段：只做一件事 —— 确认表确实存在
 *
 * ============================================================================
 * 【为什么"表存不存在"这个检查必须放在这里？】
 * ============================================================================
 *
 * 这是 DROP TABLE 和 CREATE TABLE 最关键的区别。回想一下 Stmt 层的定位：
 * 它是 parse 之后、optimize 之前的一站，手里握着 Db*，是整条链路上
 * **第一个既有完整语义信息、又能访问数据库当前状态**的地方。
 *
 * 所以：
 *   ① 语法层（yacc_sql.y:279）只能确认"DROP TABLE 后面跟了个标识符"，
 *      它不知道数据库里有没有这张表 —— 它连 Db 对象都拿不到。
 *   ② Executor 层（drop_table_executor.cpp）已经到了"执行"阶段，
 *      如果那时才发现表不存在，就是执行期报错而不是语义报错，
 *      而且 Db::drop_table 里还得再判一次，职责混乱。
 *   ③ 放在 Stmt 层最合适：此时 SQL 还没被优化、任何副作用都还没发生，
 *      判空失败可以直接 return，干净地终止整条链路
 *      （session_stage.cpp:94-98 的 resolve_stage 会把这个 rc 一路往上抛）。
 *
 * ============================================================================
 * 【为什么返回 SCHEMA_TABLE_NOT_EXIST 而不是 SUCCESS？】
 * ============================================================================
 *
 * SQL 标准要求 DROP 一个不存在的表报错（MySQL 也是这个行为，除非写
 * DROP TABLE IF EXISTS）。验收用例 primary-drop-table.test 第 24-25 行
 * 连续两次 DROP TABLE Drop_table_4，就是专门测这个：
 *
 *   DROP TABLE Drop_table_4;      -- 第一次：表存在 → SUCCESS
 *   DROP TABLE Drop_table_4;      -- 第二次：表已没了 → 必须报错，不能静默成功
 *   DROP TABLE Drop_table_4_1;    -- 从来没建过 → 也必须报错
 *
 * 如果这里返回 SUCCESS，第二次 DROP 会被当成成功，测试就会挂。
 *
 * ============================================================================
 * 【注意这里只是"登记意图"，不执行删除】
 * ============================================================================
 *
 * 本函数只 new 一个 DropTableStmt 把表名记下来，**一个文件都没删**。
 * 真正的删除在 Db::drop_table()（db.cpp:229 附近），中间还隔着
 * command_executor.cpp 的分发和 DropTableExecutor 的转发。
 *
 * 为什么不在 Stmt 层直接删？因为 Stmt 层的唯一职责是"翻译 + 语义校验"，
 * 一旦在这里产生副作用（删文件），就无法被重复调用、无法被 explain 之类
 * 的操作安全地"只看不执行"。**语义分析必须是只读的**，这是数据库实现里
 * 很重要的一条纪律：任何"改变状态"的动作都必须留到执行阶段。
 */
RC DropTableStmt::create(Db *db, const DropTableSqlNode &drop_table, Stmt *&stmt)
{
  if (db == nullptr) {
    LOG_WARN("invalid argument. db is null");
    return RC::INVALID_ARGUMENT;
  }

  if (drop_table.relation_name.empty()) {
    LOG_WARN("invalid argument. relation name is empty");
    return RC::INVALID_ARGUMENT;
  }

  // 语义检查：表必须存在，否则直接报错（drop 一个不存在的表要返回错误，不能静默成功）
  if (db->find_table(drop_table.relation_name.c_str()) == nullptr) {
    LOG_WARN("table not exist. table=%s", drop_table.relation_name.c_str());
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  stmt = new DropTableStmt(drop_table.relation_name);
  return RC::SUCCESS;
}
