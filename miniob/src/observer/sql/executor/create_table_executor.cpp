/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2023/6/13.
//

#include "sql/executor/create_table_executor.h"

#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"
#include "sql/stmt/create_table_stmt.h"
#include "storage/db/db.h"

/**
 * @brief 执行 CREATE TABLE
 *
 * ============================================================================
 * 【为什么"建表"只需要这十几行代码？因为它走的是 DDL 通道，不是查询通道】
 * ============================================================================
 *
 * 一条 SQL 从进入 observer 到执行完，在 SessionStage::handle_sql() 里要过 5 关：
 *   query_cache → parse → resolve → optimize → execute
 *
 * CREATE TABLE 在第 5 关 ExecuteStage 被分流到这里，原因在 execute_stage.cpp:32-42：
 *
 *     const unique_ptr<PhysicalOperator> &physical_operator = sql_event->physical_operator();
 *     if (physical_operator != nullptr) { ... 走算子 ... }      // SELECT/INSERT/DELETE
 *     CommandExecutor command_executor;  rc = executor.execute(); // ← CREATE/DROP 落到这里
 *
 * 而 physical_operator 只有 OptimizeStage 才会设置（optimize_stage.cpp:77
 * 的 sql_event->set_operator(...)）。对 CREATE TABLE 来说，逻辑计划生成器
 * 在 logical_plan_generator.cpp:76 直接返回 RC::UNIMPLEMENTED（它的 switch 里
 * 只有 CALC/SELECT/INSERT/DELETE/EXPLAIN 五个 case），算子从未被生成，
 * physical_operator 保持 nullptr，于是自动掉进 CommandExecutor 这条分支。
 *
 * 所以：**"没有查询计划"本身就是 DDL 的分流机制**，不是 bug，是设计。
 * 这也是为什么 CREATE TABLE 不需要写在 sql/operator/ 里 —— 它不产生任何数据行，
 * 不存在"扫描/过滤/投影"这种迭代式执行，没必要套用火山模型。
 *
 * ============================================================================
 * 【真正的建表动作在哪】
 * ============================================================================
 *
 * 这里只做一件事：把 CreateTableStmt 里携带的信息（表名、字段列表、主键、
 * 存储格式）原样转交给 Db 层，然后直接返回 RC。分三层：
 *
 *   CreateTableExecutor::execute()   ← 你在这里：SQL 层，只做转发
 *        ↓
 *   Db::create_table()               ← 内存层：new Table、登记到 opened_tables_ 哈希表
 *        ↓
 *   Table::create()                  ← 物理层：写 .table 元数据文件、创建 .data 数据文件
 *        ↓
 *   HeapTableEngine::open()          ← 引擎层：真正打开 buffer pool、挂上记录管理器
 *
 * 注意返回值语义：成功返回 RC::SUCCESS，失败返回具体错误码
 * （最常见的是 RC::SCHEMA_TABLE_EXIST —— 表已存在）。
 * ExecuteStage 会把 rc 塞进 SqlResult（execute_stage.cpp:43
 * 的 set_return_code(rc)），客户端据此打印 SUCCESS 或错误信息。
 *
 * 另外 command_executor.cpp:97-100 还有一段：因为 CREATE_TABLE 属于
 * stmt_type_ddl()（见 stmt.cpp:35-47），执行成功后会强制调一次 db->sync()
 * 把元数据刷盘，保证崩溃后重启能重新发现这张表。这个你也不用管，已生效。
 */
RC CreateTableExecutor::execute(SQLStageEvent *sql_event)
{
  Stmt    *stmt    = sql_event->stmt();
  Session *session = sql_event->session_event()->session();
  ASSERT(stmt->type() == StmtType::CREATE_TABLE,
      "create table executor can not run this command: %d",
      static_cast<int>(stmt->type()));

  CreateTableStmt *create_table_stmt = static_cast<CreateTableStmt *>(stmt);

  const char *table_name = create_table_stmt->table_name().c_str();
  RC rc = session->get_current_db()->create_table(table_name, create_table_stmt->attr_infos(), create_table_stmt->primary_keys(), create_table_stmt->storage_format());

  return rc;
}