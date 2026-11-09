/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "common/type/data_type.h"

/**
 * @brief 日期类型
 * @ingroup DataType
 * @details 内部用 int32 存储，编码为 YYYYMMDD（例如 2020-01-21 存成 20200121）。
 *
 * 为什么这样设计：
 *   1) 长度固定 4 字节，和 INT 一样，记录布局、页内偏移、索引键长都不需要特殊处理；
 *   2) YYYYMMDD 的整数大小顺序与日期先后顺序完全一致，
 *      因此日期比较直接退化为整数比较，B+ 树索引的键比较也自动正确；
 *   3) 不需要额外的结构体或字符串比较，实现最简单且没有坑。
 *
 * 对外表现（to_string / 与字符串比较）统一为 "YYYY-MM-DD" 零填充格式，
 * 与 MySQL 的输出一致。
 */
class DateType : public DataType
{
public:
  DateType() : DataType(AttrType::DATES) {}
  virtual ~DateType() {}

  int compare(const Value &left, const Value &right) const override;
  int compare(const Column &left, const Column &right, int left_idx, int right_idx) const override;

  RC cast_to(const Value &val, AttrType type, Value &result) const override;

  int cast_cost(const AttrType type) override
  {
    if (type == AttrType::DATES) {
      return 0;
    } else if (type == AttrType::CHARS) {
      return 1;
    }
    // 不支持隐式转成数值类型，避免与 INT 之间产生二义性
    return INT32_MAX;
  }

  RC set_value_from_str(Value &val, const string &data) const override;

  RC to_string(const Value &val, string &result) const override;

public:
  /**
   * @brief 解析 "YYYY-M-D" 形式的字符串（月/日允许不补零）
   * @param[in]  s     输入字符串，允许前导空格
   * @param[out] date  解析成功时写入 YYYYMMDD 形式的整数
   * @return true 表示是合法日期；false 表示格式错误或日期非法
   *         （例如 2017-2-29、2017-21-29、2017-12-32、2017-11-31 都必须返回 false）
   */
  static bool parse_date(const char *s, int32_t &date);

  /// @brief 把 YYYYMMDD 整数格式化为 "YYYY-MM-DD"（月、日补零）
  static string format_date(int32_t date);

  /// @brief 判断给定年月日是否为合法日期（含闰年判断）
  static bool is_valid_date(int year, int month, int day);

  /// @brief 将 Value 归一化为 YYYYMMDD 整数；无法转换时返回 false
  static bool value_to_int(const Value &val, int32_t &date);
};
