/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

// Created by wangyunlai.wyl on 2021/5/18.
//

#pragma once

#include "common/lang/string.h"

/**
 * ============================================================================
 * 【MiniOB 的"命名约定就是元数据"】
 * ============================================================================
 *
 * 这一个文件只有 5 个常量和 5 个拼路径的函数，却是 create table / drop table
 * 能工作的基石。原因：**MiniOB 没有系统表（没有 information_schema）**，
 * 它靠"文件名"来表达"表的存在"。
 *
 * 一个数据库目录（~/work/miniob/SYS/）里长这样：
 *
 *   SYS.db                  ← 数据库自身元数据，只有一个数字：checkpoint LSN
 *   t1.table                ← 表 t1 的元数据（TableMeta 序列化后的二进制）
 *   t1.data                 ← 表 t1 的记录数据
 *   t1.lob                  ← 表 t1 的大字段数据（text 类型用）
 *   t1-idx_id.index         ← 表 t1 上名为 idx_id 的 B+ 树索引
 *   t2.table / t2.data      ← 表 t2 ...
 *
 * 由此推出三个直接影响 DDL 实现的结论：
 *
 *   ① 「表存在」的物理定义 = 存在 <表名>.table 文件。
 *      Db::open_all_tables()（db.cpp:198）启动时就是
 *      list_file(path, TABLE_META_FILE_PATTERN, ...) 扫所有 *.table 文件，
 *      一个一个 Table::open() 加载回来。
 *
 *      → 所以 create table 成功 = .table 文件被创建
 *      → 所以 drop table 必须删除 .table 文件，否则重启后表会复活
 *
 *   ② 「索引名」不能重复但可以跨表重名 ———— 因为索引文件名是
 *      <表名>-<索引名>.index，表名已经做了命名空间隔离。
 *      注意分隔符是 **减号**，不是点号，这是刻意区分：
 *        点号（.）标识"某个表的某种资源"
 *        减号（-）标识"某个表的某个索引"
 *      这个区别正是 Db::drop_table 里用两个前缀做匹配的依据：
 *        dot_prefix  = "t1."  → 命中 t1.table / t1.data / t1.lob
 *        dash_prefix = "t1-"  → 再要求以 .index 结尾 → 命中 t1-idx_id.index
 *
 *   ③ 为什么用"前缀匹配 + 遍历目录"而不是直接拼出文件名？
 *      因为索引数量是运行时才知道的（.table 文件里的 index_num），
 *      而且索引可能有任意个。拼文件名需要先把所有索引名读出来；
 *      而遍历目录一次就能拿到全部，代码更短且不会漏。
 *      同时前缀匹配天然规避了"误删同前缀表"的风险：
 *        drop table class  不会碰到 class_room.table（因为 "class_" != "class."）
 */
static constexpr const char *DB_META_SUFFIX          = ".db";
static constexpr const char *TABLE_META_SUFFIX       = ".table";
static constexpr const char *TABLE_META_FILE_PATTERN = ".*\\.table$";
static constexpr const char *TABLE_DATA_SUFFIX       = ".data";
static constexpr const char *TABLE_INDEX_SUFFIX      = ".index";
static constexpr const char *TABLE_LOB_SUFFIX        = ".lob";

string db_meta_file(const char *base_dir, const char *db_name);
string table_meta_file(const char *base_dir, const char *table_name);
string table_data_file(const char *base_dir, const char *table_name);
string table_index_file(const char *base_dir, const char *table_name, const char *index_name);
string table_lob_file(const char *base_dir, const char *table_name);
