/* Copyright (c) 2021 Xie Meiyi(xiemeiyi@hust.edu.cn) and OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Meiyi & Longda & Wangyunlai on 2021/5/12.
//

#include "storage/db/db.h"

#include <fcntl.h>
#include <sys/stat.h>

#include "common/lang/string.h"
#include "common/log/log.h"
#include "common/os/path.h"
#include "common/global_context.h"
#include "storage/common/meta_util.h"
#include "storage/table/table.h"
#include "storage/table/table_meta.h"
#include "storage/trx/trx.h"
#include "storage/clog/disk_log_handler.h"
#include "storage/clog/integrated_log_replayer.h"

using namespace common;

Db::~Db()
{
  for (auto &iter : opened_tables_) {
    delete iter.second;
  }

  if (log_handler_) {
    // 停止日志并等待写入完成
    log_handler_->stop();
    log_handler_->await_termination();
    log_handler_.reset();
  }
  if (lsm_) {
    delete lsm_;
    lsm_ = nullptr;
  }
  LOG_INFO("Db has been closed: %s", name_.c_str());
}

RC Db::init(const char *name, const char *dbpath, const char *trx_kit_name, const char *log_handler_name, const char *storage_engine)
{
  RC rc = RC::SUCCESS;

  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init DB, name cannot be empty");
    return RC::INVALID_ARGUMENT;
  }

  if (!filesystem::is_directory(dbpath)) {
    LOG_ERROR("Failed to init DB, path is not a directory: %s", dbpath);
    return RC::INVALID_ARGUMENT;
  }

  oceanbase::ObLsmOptions options;
  filesystem::path lsm_path = filesystem::path(dbpath) / "lsm";
  filesystem::create_directory(lsm_path);

  rc = oceanbase::ObLsm::open(options, lsm_path, &lsm_);
  if (OB_FAIL(rc)) {
    LOG_ERROR("failed to open lsm. dbpath=%s, rc=%s", dbpath, strrc(rc));
    return rc;
  }

  TrxKit *trx_kit = TrxKit::create(trx_kit_name, this);
  if (trx_kit == nullptr) {
    LOG_ERROR("Failed to create trx kit: %s", trx_kit_name);
    return RC::INVALID_ARGUMENT;
  }

  trx_kit_.reset(trx_kit);

  storage_engine_ = storage_engine;

  buffer_pool_manager_ = make_unique<BufferPoolManager>();
  auto dblwr_buffer    = make_unique<DiskDoubleWriteBuffer>(*buffer_pool_manager_);

  const char      *double_write_buffer_filename  = "dblwr.db";
  filesystem::path double_write_buffer_file_path = filesystem::path(dbpath) / double_write_buffer_filename;
  rc                                             = dblwr_buffer->open_file(double_write_buffer_file_path.c_str());
  if (OB_FAIL(rc)) {
    LOG_ERROR("Failed to open double write buffer file. file=%s, rc=%s",
              double_write_buffer_file_path.c_str(), strrc(rc));
    return rc;
  }

  rc = buffer_pool_manager_->init(std::move(dblwr_buffer));
  if (OB_FAIL(rc)) {
    LOG_ERROR("Failed to init buffer pool manager. dbpath=%s, rc=%s", dbpath, strrc(rc));
    return rc;
  }

  filesystem::path clog_path       = filesystem::path(dbpath) / "clog";
  LogHandler      *tmp_log_handler = nullptr;
  rc                               = LogHandler::create(log_handler_name, tmp_log_handler);
  if (OB_FAIL(rc)) {
    LOG_ERROR("Failed to create log handler: %s", log_handler_name);
    return rc;
  }
  log_handler_.reset(tmp_log_handler);

  rc = log_handler_->init(clog_path.c_str());
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to init log handler. dbpath=%s, rc=%s", dbpath, strrc(rc));
    return rc;
  }

  name_ = name;
  path_ = dbpath;

  // 加载数据库本身的元数据
  rc = init_meta();
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to init meta. dbpath=%s, rc=%s", dbpath, strrc(rc));
    return rc;
  }

  // 打开所有表
  // 在实际生产数据库中，直接打开所有表，可能耗时会比较长
  rc = open_all_tables();
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to open all tables. dbpath=%s, rc=%s", dbpath, strrc(rc));
    return rc;
  }

  rc = init_dblwr_buffer();
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to init dblwr buffer. rc = %s", strrc(rc));
    return rc;
  }

  // 尝试恢复数据库，重做redo日志
  rc = recover();
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to recover db. dbpath=%s, rc=%s", dbpath, strrc(rc));
    return rc;
  }

  return rc;
}

/**
 * @brief 建表的"内存层"：先查重，再 new 一个 Table，最后登记进哈希表
 *
 * ============================================================================
 * 【为什么 create table 之后 show tables 就能看到这张表？】
 * ============================================================================
 *
 * 因为 Db 用一个哈希表 opened_tables_ 维护"当前数据库里有哪些表"：
 *
 *     unordered_map<string, Table *> opened_tables_;   // db.h:139
 *
 * 第 174 行 opened_tables_[table_name] = table; 这一句就是登记动作。
 * 之后所有按名字找表的地方（Db::find_table、Db::all_tables）都查这个哈希表：
 *
 *   - show tables   → all_tables()            → 遍历 opened_tables_ 取 key
 *   - desc t        → DescTableStmt::create() → find_table(t) 不为空才继续
 *   - insert/select → 绑定字段时也要先 find_table()
 *
 * 所以"表存在"在 MiniOB 里的定义就是：**这个名字在 opened_tables_ 这张哈希表里**。
 * 这也解释了 drop table 为什么必须 opened_tables_.erase() —— 不从哈希表里摘掉，
 * show tables 依然能看到它。
 *
 * ============================================================================
 * 【为什么重复建表会报错？】
 * ============================================================================
 *
 * 第 157 行先查 opened_tables_.count(table_name) != 0 就返回
 * RC::SCHEMA_TABLE_EXIST —— 这是内存层的查重。
 *
 * 而 Table::create() 里还有第二道防线（table.cpp:69 的 open(O_CREAT|O_EXCL)），
 * 那是磁盘层的查重：文件已存在就 EEXIST。双保险的意义在于
 * 「内存里没有但磁盘上残留文件」这种异常场景也能拦住，
 * 避免把两份不同的元数据写进同一个文件。
 *
 * ============================================================================
 * 【table_id 怎么来的】
 * ============================================================================
 *
 * 第 165 行 next_table_id_++ ，这个是单调递增的全局计数器，
 * 启动时由 open_all_tables() 根据磁盘上已有的表推算出来（db.cpp:226-228），
 * 保证重启后不会撞号。table_id 会写进 .table 元数据文件，
 * redo log 也用它来标识是哪张表（见 db.h:145 的注释）。
 */
RC Db::create_table(const char *table_name, span<const AttrInfoSqlNode> attributes, const vector<string>& primary_keys, const StorageFormat storage_format)
{
  RC rc = RC::SUCCESS;
  // check table_name
  if (opened_tables_.count(table_name) != 0) {
    LOG_WARN("%s has been opened before.", table_name);
    return RC::SCHEMA_TABLE_EXIST;
  }

  // 文件路径可以移到Table模块
  string  table_file_path = table_meta_file(path_.c_str(), table_name);
  Table  *table           = new Table();
  int32_t table_id        = next_table_id_++;
  rc = table->create(this, table_id, table_file_path.c_str(), table_name, path_.c_str(), attributes, primary_keys, storage_format,
                     get_storage_engine());
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Failed to create table %s.", table_name);
    delete table;
    return rc;
  }

  opened_tables_[table_name] = table;
  LOG_INFO("Create table success. table name=%s, table_id:%d", table_name, table_id);
  return RC::SUCCESS;
}

/**
 * @brief 删表的"内存层 + 磁盘层"：摘哈希表、析构对象、unlink 文件
 *
 * ============================================================================
 * 【drop table 必须做对三件事，缺一不可】
 * ============================================================================
 *
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │ ① 从 opened_tables_ 哈希表里 erase                                 │
 *   │    → 让 show tables / find_table / desc 都看不到这张表              │
 *   │    → 漏掉的后果：SQL 层面"删不掉"，show tables 依然列出来           │
 *   ├─────────────────────────────────────────────────────────────────────┤
 *   │ ② delete table（触发 Table → HeapTableEngine 的连环析构）           │
 *   │    → 关数据文件句柄、释放 buffer pool、释放所有 B+ 树索引对象       │
 *   │    → 漏掉的后果：内存泄漏 + 文件句柄泄漏 + buffer pool 里的脏页      │
 *   │                 再也刷不出去，之后同名表的重建会读到脏数据          │
 *   ├─────────────────────────────────────────────────────────────────────┤
 *   │ ③ 删除磁盘上的 .table / .data / .lob / -<索引>.index 文件           │
 *   │    → 让这张表"物理上消失"                                          │
 *   │    → 漏掉的后果：重启后 Db::open_all_tables() 扫描 *.table 把它     │
 *   │                 重新加载回来（表复活）；且再也无法 create 同名表     │
 *   │                 （Table::create 的 O_EXCL 报 SCHEMA_TABLE_EXIST）    │
 *   └─────────────────────────────────────────────────────────────────────┘
 *
 * 注意 ① 和 ② 的顺序不能反。如果先 delete table 再 erase，erase 时
 * 哈希表里存的 Table* 已经变成野指针；虽然 erase 本身不访问它，
 * 但任何后续误用都是 use-after-free。db.cpp:221 就有一条历史注释
 * 说"原本先删除 table 后调用 table->name()，犯了 use-after-free 的错误"，
 * 而本项目默认开了 ASAN（CMakeLists.txt:24 ENABLE_ASAN ON），
 * 这类 bug 会被当场抓住。
 *
 * 所以本函数刻意写成：取出指针 → erase → delete → 之后只用入参 table_name。
 * **delete 之后绝不碰 table 的任何成员**，包括 table->name()。
 *
 * ============================================================================
 * 【为什么 ③ 要用"遍历目录 + 前缀匹配"而不是直接拼文件名】
 * ============================================================================
 *
 * 文件名规则见 storage/common/meta_util.h：
 *
 *   <table>.table            表的元数据（决定这张表"存不存在"）
 *   <table>.data             表的数据
 *   <table>.lob              表的大字段
 *   <table>-<index>.index    表上某个索引（索引数量运行时才知道，且名字任意）
 *
 * 前三类可以直接拼（后缀固定），但第四类不行 —— 索引有几个、各叫什么，
 * 只有读 .table 元数据才知道。与其先解析元数据再逐个拼路径，不如
 * 遍历一次目录，用两个前缀把四类一网打尽：
 *
 *   dot_prefix  = "<table>."  → 命中 .table / .data / .lob
 *   dash_prefix = "<table>-"  → 再要求以 ".index" 结尾 → 命中索引文件
 *
 * 前缀匹配顺带解决了"误删"问题：drop table class 不会碰到 class_room.table，
 * 因为 "class_room" 既不以 "class." 开头，也不以 "class-" 开头。
 */
RC Db::drop_table(const char *table_name)
{
  if (table_name == nullptr || common::is_blank(table_name)) {
    LOG_WARN("invalid table name");
    return RC::INVALID_ARGUMENT;
  }

  unordered_map<string, Table *>::iterator iter = opened_tables_.find(table_name);
  if (iter == opened_tables_.end()) {
    LOG_WARN("table not exist. db=%s, table=%s", name_.c_str(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  // 1. 先把 table 从内存索引中摘掉，再析构。
  //    Table 析构 -> HeapTableEngine 析构，会 delete record_handler_、
  //    data_buffer_pool_->close_file()、并 delete 所有 Index，
  //    所以这里的 delete 已经把文件句柄和 buffer pool 里的页都收拾干净了。
  //    注意：delete 之后不能再访问 table 的任何成员，只能用入参 table_name。
  Table *table = iter->second;
  opened_tables_.erase(iter);
  delete table;
  table = nullptr;

  // 2. 删除磁盘上属于这张表的文件。命名规则见 storage/common/meta_util.h：
  //      <table>.table / <table>.data / <table>.lob / <table>-<index_name>.index
  //    用前缀匹配而不是直接拼文件名，这样 .lob 和任意名字的索引文件都能覆盖到，
  //    同时又不会误删 class_room.table 这种前缀相同但名字不同的表。
  const string dot_prefix   = string(table_name) + ".";
  const string dash_prefix  = string(table_name) + "-";
  const string index_suffix = TABLE_INDEX_SUFFIX;

  int        removed = 0;
  error_code iter_ec;
  for (const auto &entry : filesystem::directory_iterator(path_.c_str(), iter_ec)) {
    const string filename = entry.path().filename().string();

    bool matched = (filename.compare(0, dot_prefix.size(), dot_prefix) == 0);
    if (!matched) {
      matched = (filename.compare(0, dash_prefix.size(), dash_prefix) == 0 &&
                 filename.size() >= index_suffix.size() &&
                 filename.compare(filename.size() - index_suffix.size(), index_suffix.size(), index_suffix) == 0);
    }
    if (!matched) {
      continue;
    }

    error_code rm_ec;
    filesystem::remove(entry.path(), rm_ec);
    if (rm_ec) {
      LOG_WARN("failed to remove file %s: %s", filename.c_str(), rm_ec.message().c_str());
    } else {
      LOG_INFO("removed file %s", filename.c_str());
      ++removed;
    }
  }
  if (iter_ec) {
    LOG_WARN("failed to iterate dir %s: %s", path_.c_str(), iter_ec.message().c_str());
  }

  LOG_INFO("Drop table success. db=%s, table=%s, removed_files=%d", name_.c_str(), table_name, removed);
  return RC::SUCCESS;
}

Table *Db::find_table(const char *table_name) const
{
  unordered_map<string, Table *>::const_iterator iter = opened_tables_.find(table_name);
  if (iter != opened_tables_.end()) {
    return iter->second;
  }
  return nullptr;
}

Table *Db::find_table(int32_t table_id) const
{
  for (auto pair : opened_tables_) {
    if (pair.second->table_id() == table_id) {
      return pair.second;
    }
  }
  return nullptr;
}

RC Db::open_all_tables()
{
  vector<string> table_meta_files;

  int ret = list_file(path_.c_str(), TABLE_META_FILE_PATTERN, table_meta_files);
  if (ret < 0) {
    LOG_ERROR("Failed to list table meta files under %s.", path_.c_str());
    return RC::IOERR_READ;
  }

  RC rc = RC::SUCCESS;
  for (const string &filename : table_meta_files) {
    Table *table = new Table();
    rc           = table->open(this, filename.c_str(), path_.c_str());
    if (rc != RC::SUCCESS) {
      delete table;
      LOG_ERROR("Failed to open table. filename=%s", filename.c_str());
      return rc;
    }

    if (opened_tables_.count(table->name()) != 0) {
      LOG_ERROR("Duplicate table with difference file name. table=%s, the other filename=%s",
          table->name(), filename.c_str());
      // 在这里原本先删除table后调用table->name()方法，犯了use-after-free的错误
      delete table;
      return RC::INTERNAL;
    }

    if (table->table_id() >= next_table_id_) {
      next_table_id_ = table->table_id() + 1;
    }
    opened_tables_[table->name()] = table;
    LOG_INFO("Open table: %s, file: %s", table->name(), filename.c_str());
  }

  LOG_INFO("All table have been opened. num=%d", opened_tables_.size());
  return rc;
}

const char *Db::name() const { return name_.c_str(); }

void Db::all_tables(vector<string> &table_names) const
{
  for (const auto &table_item : opened_tables_) {
    table_names.emplace_back(table_item.first);
  }
}

RC Db::sync()
{
  RC rc = RC::SUCCESS;
  // 调用所有表的sync函数刷新数据到磁盘
  for (const auto &table_pair : opened_tables_) {
    Table *table = table_pair.second;
    rc           = table->sync();
    if (rc != RC::SUCCESS) {
      LOG_ERROR("Failed to flush table. table=%s.%s, rc=%d:%s", name_.c_str(), table->name(), rc, strrc(rc));
      return rc;
    }
    LOG_INFO("Successfully sync table db:%s, table:%s.", name_.c_str(), table->name());
  }

  auto dblwr_buffer = static_cast<DiskDoubleWriteBuffer *>(buffer_pool_manager_->get_dblwr_buffer());
  rc                = dblwr_buffer->flush_page();
  LOG_INFO("double write buffer flush pages ret=%s", strrc(rc));

  /*
  在sync期间，不允许有未完成的事务，也不允许开启新的事物。
  这个约束不是从程序层面处理的，而是认为的约束。
  */
  LSN current_lsn = log_handler_->current_lsn();
  rc              = log_handler_->wait_lsn(current_lsn);
  if (OB_FAIL(rc)) {
    LOG_ERROR("Failed to wait lsn. lsn=%ld, rc=%d:%s", current_lsn, rc, strrc(rc));
    return rc;
  }

  check_point_lsn_ = current_lsn;
  rc               = flush_meta();
  if (OB_FAIL(rc)) {
    LOG_ERROR("Failed to flush meta. db=%s, rc=%d:%s", name_.c_str(), rc, strrc(rc));
    return rc;
  }
  LOG_INFO("Successfully sync db. db=%s", name_.c_str());
  return rc;
}

RC Db::recover()
{
  LOG_TRACE("db recover begin. check_point_lsn=%d", check_point_lsn_);

  LogReplayer *trx_log_replayer = trx_kit_->create_log_replayer(*this, *log_handler_);
  if (trx_log_replayer == nullptr) {
    LOG_ERROR("Failed to create trx log replayer.");
    return RC::INTERNAL;
  }

  IntegratedLogReplayer log_replayer(*buffer_pool_manager_, unique_ptr<LogReplayer>(trx_log_replayer));
  RC                    rc = log_handler_->replay(log_replayer, check_point_lsn_ /*start_lsn*/);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to replay log. rc=%s", strrc(rc));
    return rc;
  }

  rc = log_handler_->start();
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to start log handler. rc=%s", strrc(rc));
    return rc;
  }

  rc = log_replayer.on_done();
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to on_done. rc=%s", strrc(rc));
    return rc;
  }

  LOG_INFO("Successfully recover db. db=%s checkpoint_lsn=%d", name_.c_str(), check_point_lsn_);
  return rc;
}

RC Db::init_meta()
{
  filesystem::path db_meta_file_path = db_meta_file(path_.c_str(), name_.c_str());
  if (!filesystem::exists(db_meta_file_path)) {
    check_point_lsn_ = 0;
    LOG_INFO("Db meta file not exist. db=%s, file=%s", name_.c_str(), db_meta_file_path.c_str());
    return RC::SUCCESS;
  }

  RC  rc = RC::SUCCESS;
  int fd = open(db_meta_file_path.c_str(), O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Failed to open db meta file. db=%s, file=%s, errno=%s", 
              name_.c_str(), db_meta_file_path.c_str(), strerror(errno));
    return RC::IOERR_READ;
  }

  char buffer[1024];
  int  n = read(fd, buffer, sizeof(buffer));
  if (n < 0) {
    LOG_ERROR("Failed to read db meta file. db=%s, file=%s, errno=%s", 
              name_.c_str(), db_meta_file_path.c_str(), strerror(errno));
    rc = RC::IOERR_READ;
  } else {
    if (n >= static_cast<int>(sizeof(buffer))) {
      LOG_WARN("Db meta file is too large. db=%s, file=%s, buffer size=%ld", 
               name_.c_str(), db_meta_file_path.c_str(), sizeof(buffer));
      return RC::IOERR_TOO_LONG;
    }

    buffer[n]        = '\0';
    check_point_lsn_ = atoll(buffer);  // 当前元数据就这一个数字
    LOG_INFO("Successfully read db meta file. db=%s, file=%s, check_point_lsn=%ld", 
             name_.c_str(), db_meta_file_path.c_str(), check_point_lsn_);
  }
  close(fd);

  return rc;
}

RC Db::flush_meta()
{
  // 将数据库元数据刷新到磁盘
  // 先创建一个临时文件，将元数据写入临时文件
  // 然后再将临时文件修改为正式文件

  filesystem::path meta_file_path      = db_meta_file(path_.c_str(), name_.c_str());  // 正式文件名
  filesystem::path temp_meta_file_path = meta_file_path;                              // 临时文件名
  temp_meta_file_path += ".tmp";

  RC  rc = RC::SUCCESS;
  int fd = open(temp_meta_file_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0) {
    LOG_ERROR("Failed to open db meta file. db=%s, file=%s, errno=%s", 
              name_.c_str(), temp_meta_file_path.c_str(), strerror(errno));
    return RC::IOERR_WRITE;
  }

  string buffer = to_string(check_point_lsn_);
  int    n      = write(fd, buffer.c_str(), buffer.size());
  if (n < 0) {
    LOG_ERROR("Failed to write db meta file. db=%s, file=%s, errno=%s", 
              name_.c_str(), temp_meta_file_path.c_str(), strerror(errno));
    rc = RC::IOERR_WRITE;
  } else if (n != static_cast<int>(buffer.size())) {
    LOG_ERROR("Failed to write db meta file. db=%s, file=%s, buffer size=%ld, write size=%d", 
              name_.c_str(), temp_meta_file_path.c_str(), buffer.size(), n);
    rc = RC::IOERR_WRITE;
  } else {
    error_code ec;
    filesystem::rename(temp_meta_file_path, meta_file_path, ec);
    if (ec) {
      LOG_ERROR("Failed to rename db meta file. db=%s, file=%s, errno=%s", 
                name_.c_str(), temp_meta_file_path.c_str(), ec.message().c_str());
      rc = RC::IOERR_WRITE;
    } else {

      LOG_INFO("Successfully write db meta file. db=%s, file=%s, check_point_lsn=%ld", 
               name_.c_str(), temp_meta_file_path.c_str(), check_point_lsn_);
    }
  }

  return rc;
}

RC Db::init_dblwr_buffer()
{
  auto dblwr_buffer = static_cast<DiskDoubleWriteBuffer *>(buffer_pool_manager_->get_dblwr_buffer());
  RC   rc           = dblwr_buffer->recover();
  if (OB_FAIL(rc)) {
    LOG_ERROR("fail to recover in dblwr buffer");
    return rc;
  }

  return RC::SUCCESS;
}

LogHandler        &Db::log_handler() { return *log_handler_; }
BufferPoolManager &Db::buffer_pool_manager() { return *buffer_pool_manager_; }
TrxKit            &Db::trx_kit() { return *trx_kit_; }
