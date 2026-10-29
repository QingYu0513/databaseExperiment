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

#include "common/log/log.h"
#include "common/types.h"
#include "sql/stmt/create_table_stmt.h"
#include "event/sql_debug.h"

/**
 * @brief CREATE TABLE 的语义分析阶段
 *
 * ============================================================================
 * 【为什么这里"不检查表是否已存在"？—— 和 DropTableStmt 刚好相反】
 * ============================================================================
 *
 * 这个函数异常简单：把解析出来的 CreateTableSqlNode 搬进 CreateTableStmt，
 * 一个字段都没校验（连"表名不能为空""字段不能重复"都不管），
 * 只额外把 storage_format 字符串转成枚举。
 *
 * 对比一下 DropTableStmt::create()（drop_table_stmt.cpp），它第一件事就是
 * db->find_table() 判空，不存在直接返回 RC::SCHEMA_TABLE_NOT_EXIST。
 * 为什么两条语句的语义检查位置不一样？
 *
 * 因为两者"依赖什么状态"不同：
 *
 *   DROP TABLE  要求目标表必须已存在 → 必须查 Db 的状态 → 只能在能访问 Db 的
 *               Stmt 层（这里 Db* db 参数就是给它用的）做检查。
 *
 *   CREATE TABLE 要求目标表必须不存在 → 这个检查在 Db::create_table() 第 157 行
 *               用 opened_tables_.count() 完成更自然，因为那里同时持有内存表和
 *               磁盘文件的视角；再做一次就重复了。
 *
 * 结论：**Stmt 层的职责是"把语法树翻译成带语义的对象，并检查那些必须靠
 * 数据库当前状态才能判断的约束"**。能不能建、建在哪，属于存储层的判断，
 * 交给 Db 层更合适。
 *
 * ============================================================================
 * 【storage_format 是什么】
 * ============================================================================
 *
 * 两种行存格式，来自编译期开关和 SQL 里可选的 WITH STORAGE FORMAT 子句：
 *   ROW_FORMAT（默认）— 一行记录在页内连续存放，传统行存
 *   PAX_FORMAT        — 页内按列分组存放（Partition Attributes Across），
 *                       为向量化执行准备的混合格式
 * get_storage_format() 把 SQL 里的字符串转成枚举，只认 "ROW"/"PAX"，
 * 空字符串默认 ROW，其它一律 UNKNOWN_FORMAT → 调用方返回 RC::INVALID_ARGUMENT。
 */
RC CreateTableStmt::create(Db *db, const CreateTableSqlNode &create_table, Stmt *&stmt)
{
  StorageFormat storage_format = get_storage_format(create_table.storage_format.c_str());
  if (storage_format == StorageFormat::UNKNOWN_FORMAT) {
    return RC::INVALID_ARGUMENT;
  }
  stmt = new CreateTableStmt(create_table.relation_name, create_table.attr_infos, create_table.primary_keys, storage_format);
  sql_debug("create table statement: table name %s", create_table.relation_name.c_str());
  return RC::SUCCESS;
}

StorageFormat CreateTableStmt::get_storage_format(const char *format_str) {
  StorageFormat format = StorageFormat::UNKNOWN_FORMAT;
  if (strlen(format_str) == 0) {
    format = StorageFormat::ROW_FORMAT;
  } else if (0 == strcasecmp(format_str, "ROW")) {
    format = StorageFormat::ROW_FORMAT;
  } else if (0 == strcasecmp(format_str, "PAX")) {
    format = StorageFormat::PAX_FORMAT;
  } else {
    format = StorageFormat::UNKNOWN_FORMAT;
  }
  return format;
}
