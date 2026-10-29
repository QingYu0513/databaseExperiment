/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/executor/drop_table_executor.h"

#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"
#include "sql/stmt/drop_table_stmt.h"
#include "storage/db/db.h"

/**
 * @brief 执行 DROP TABLE：SQL 层的最后一站，只做转发
 *
 * ============================================================================
 * 【它和 CreateTableExecutor 是同一个套路，为什么？】
 * ============================================================================
 *
 * 因为两者都是 DDL，都走"命令执行器"通道，都在 execute_stage.cpp:32-42
 * 因为 physical_operator == nullptr 而被分流过来（详见 create_table_executor.cpp
 * 顶部的长注释，那里把分流机制讲透了）。
 *
 * 对比一下 SELECT 的执行路径，能看清 DDL 有多"轻"：
 *
 *   SELECT       parse → Stmt → 逻辑计划 → 规则重写（谓词下推等）
 *                → 物理计划 → 算子树（TableScan→Predicate→Project）
 *                → 逐行 next() 拉取 → 填 TupleSchema → 输出结果集
 *                涉及文件：logical_plan_generator / rewriter /
 *                         physical_plan_generator / sql/operator/*.cpp
 *
 *   DROP TABLE   parse → Stmt → （优化器直接放弃）→ 本函数
 *                → Db::drop_table → 改哈希表 + 删文件
 *                涉及文件：本文件 + db.cpp，两个
 *
 * 差别根因：SELECT 要回答"哪些行符合条件"，需要一套可组合、可优化的
 * 迭代式执行框架；DROP TABLE 只是"改一条元数据记录"，一次性的动作，
 * 套用算子模型纯属浪费。
 *
 * ============================================================================
 * 【返回值怎么变成屏幕上的输出？】
 * ============================================================================
 *
 * 本函数返回的 RC 会沿这条链上抛：
 *
 *   DropTableExecutor::execute → CommandExecutor::execute 返回 rc
 *     → execute_stage.cpp:43  session_event->sql_result()->set_return_code(rc)
 *       → communicator->write_result() 判断 rc：
 *            RC::SUCCESS → 打印 "SUCCESS"
 *            其它        → 打印错误码和信息，例如
 *                          "FAILED TO ...  table not exist" 
 *
 * 所以屏幕上那句 SUCCESS 就是这里的返回值，一个字都没额外写。
 *
 * ============================================================================
 * 【ASSERT 是干什么的】
 * ============================================================================
 *
 * 第 24 行断言 stmt->type() == StmtType::DROP_TABLE，是防御式编程：
 * CommandExecutor 的 switch 已经保证了只有 DROP_TABLE 才会进来，
 * 但万一以后有人改坏了分发逻辑，断言会立刻在 Debug 版炸出来
 * （log.h:319 的 ASSERT 在 release 版会被编译成空操作）。
 * 每个 Executor 都这么写，是 MiniOB 的统一风格。
 */
RC DropTableExecutor::execute(SQLStageEvent *sql_event)
{
  Stmt    *stmt    = sql_event->stmt();
  Session *session = sql_event->session_event()->session();
  ASSERT(stmt->type() == StmtType::DROP_TABLE,
      "drop table executor can not run this command: %d",
      static_cast<int>(stmt->type()));

  DropTableStmt *drop_table_stmt = static_cast<DropTableStmt *>(stmt);
  const char    *table_name      = drop_table_stmt->table_name().c_str();

  RC rc = session->get_current_db()->drop_table(table_name);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to drop table. table=%s, rc=%s", table_name, strrc(rc));
  }
  return rc;
}
