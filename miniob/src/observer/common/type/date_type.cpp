/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/type/date_type.h"

#include <stdint.h>
#include <stdio.h>

#include "common/lang/sstream.h"
#include "common/log/log.h"
#include "common/value.h"
#include "storage/common/column.h"

///////////////////////////////////////////////////////////////////////////////
// 日期合法性判断
bool DateType::is_valid_date(int year, int month, int day)
{
  if (year < 0 || month < 1 || month > 12 || day < 1) {
    return false;
  }

  // 各月天数，2 月先按 28 天处理，闰年再补一天
  static const int days_of_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int              max_day         = days_of_month[month - 1];

  if (month == 2) {
    // 闰年规则：能被 4 整除但不能被 100 整除，或者能被 400 整除
    const bool is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
    if (is_leap) {
      max_day = 29;
    }
  }

  return day <= max_day;
}

bool DateType::parse_date(const char *s, int32_t &date)
{
  date = 0;
  if (s == nullptr) {
    return false;
  }

  // 跳过前导空格
  while (*s == ' ' || *s == '\t') {
    s++;
  }

  // 严格按 Y-M-D 三段数字解析，中间必须是 '-'，允许月/日不补零
  int         parts[3]   = {0, 0, 0};
  int         part_index = 0;
  const char *p          = s;

  for (; part_index < 3; part_index++) {
    const char *begin = p;
    while (*p >= '0' && *p <= '9') {
      parts[part_index] = parts[part_index] * 10 + (*p - '0');
      if (parts[part_index] > 9999) {
        return false;  // 单项数字过长，肯定非法
      }
      p++;
    }
    if (p == begin) {
      return false;  // 某一段没有数字
    }
    if (part_index < 2) {
      if (*p != '-') {
        return false;  // 缺少分隔符
      }
      p++;
    }
  }

  // 尾部只允许空格
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (*p != '\0') {
    return false;
  }

  const int year  = parts[0];
  const int month = parts[1];
  const int day   = parts[2];
  if (!is_valid_date(year, month, day)) {
    return false;
  }

  date = year * 10000 + month * 100 + day;
  return true;
}

string DateType::format_date(int32_t date)
{
  const int year  = date / 10000;
  const int month = (date / 100) % 100;
  const int day   = date % 100;

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
  return string(buffer);
}

bool DateType::value_to_int(const Value &val, int32_t &date)
{
  date = 0;
  switch (val.attr_type()) {
    case AttrType::DATES:
    case AttrType::INTS: {
      date = val.get_date();
      return true;
    }
    case AttrType::CHARS: {
      // 字符串需要现场解析，解析失败视为非法
      return parse_date(val.get_string().c_str(), date);
    }
    default: {
      return false;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// 比较运算
int DateType::compare(const Value &left, const Value &right) const
{
  int32_t left_date  = 0;
  int32_t right_date = 0;
  if (!value_to_int(left, left_date) || !value_to_int(right, right_date)) {
    LOG_WARN("failed to compare date value. left type=%s, right type=%s",
        attr_type_to_string(left.attr_type()),
        attr_type_to_string(right.attr_type()));
    return INT32_MAX;  // 表示无法比较
  }

  if (left_date < right_date) {
    return -1;
  }
  if (left_date > right_date) {
    return 1;
  }
  return 0;
}

int DateType::compare(const Column &left, const Column &right, int left_idx, int right_idx) const
{
  const int32_t left_date  = ((const int32_t *)left.data())[left_idx];
  const int32_t right_date = ((const int32_t *)right.data())[right_idx];

  if (left_date < right_date) {
    return -1;
  }
  if (left_date > right_date) {
    return 1;
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
// 类型转换
RC DateType::cast_to(const Value &val, AttrType type, Value &result) const
{
  int32_t date = 0;
  if (!value_to_int(val, date)) {
    LOG_WARN("failed to cast value to date. value=%s", val.to_string().c_str());
    return RC::INVALID_ARGUMENT;
  }

  switch (type) {
    case AttrType::DATES: {
      result.set_date(date);
    } break;
    case AttrType::CHARS: {
      result.set_string(format_date(date).c_str());
    } break;
    default: {
      LOG_WARN("cannot cast date to type: %s", attr_type_to_string(type));
      return RC::UNSUPPORTED;
    }
  }
  return RC::SUCCESS;
}

RC DateType::set_value_from_str(Value &val, const string &data) const
{
  int32_t date = 0;
  if (!parse_date(data.c_str(), date)) {
    LOG_WARN("invalid date string: %s", data.c_str());
    return RC::INVALID_ARGUMENT;
  }
  val.set_date(date);
  return RC::SUCCESS;
}

RC DateType::to_string(const Value &val, string &result) const
{
  int32_t date = 0;
  if (!value_to_int(val, date)) {
    LOG_WARN("failed to convert date value to string");
    return RC::INVALID_ARGUMENT;
  }
  result = format_date(date);
  return RC::SUCCESS;
}
