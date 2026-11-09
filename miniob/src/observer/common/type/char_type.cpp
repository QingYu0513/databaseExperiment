/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/lang/comparator.h"
#include "common/log/log.h"
#include "common/type/char_type.h"
#include "common/type/date_type.h"
#include "common/value.h"

int CharType::compare(const Value &left, const Value &right) const
{
  // 与日期比较时（例如 '2020-1-20' < u_date），把比较委托给日期类型，
  // 否则按字符串字典序比较会得到错误结果（"2020-1-20" > "2020-01-21"）。
  // 注意 compare 是 DataType 的普通虚函数（非静态），必须通过实例调用。
  if (right.attr_type() == AttrType::DATES) {
    DataType *date_type = DataType::type_instance(AttrType::DATES);
    const int cmp       = date_type->compare(right, left);
    return (cmp == INT32_MAX) ? INT32_MAX : -cmp;
  }

  ASSERT(left.attr_type() == AttrType::CHARS && right.attr_type() == AttrType::CHARS, "invalid type");
  return common::compare_string(
      (void *)left.value_.pointer_value_, left.length_, (void *)right.value_.pointer_value_, right.length_);
}

RC CharType::set_value_from_str(Value &val, const string &data) const
{
  val.set_string(data.c_str());
  return RC::SUCCESS;
}

RC CharType::cast_to(const Value &val, AttrType type, Value &result) const
{
  switch (type) {
    case AttrType::DATES: {
      // 字符串 -> 日期：'2020-1-21' 这种月/日不补零的写法也要支持；
      // 非法日期（2017-2-29、2017-21-29、2017-12-32、2017-11-31 等）在这里被拒绝，
      // 于是 INSERT 就会报错，正是 primary-date.test 第 4 组要验证的行为。
      int32_t date = 0;
      if (nullptr == val.value_.pointer_value_ || !DateType::parse_date(val.value_.pointer_value_, date)) {
        LOG_WARN("invalid date string: %s", val.value_.pointer_value_ == nullptr ? "(null)" : val.value_.pointer_value_);
        return RC::INVALID_ARGUMENT;
      }
      result.set_date(date);
    } break;
    default: return RC::UNIMPLEMENTED;
  }
  return RC::SUCCESS;
}

int CharType::cast_cost(AttrType type)
{
  if (type == AttrType::CHARS) {
    return 0;
  } else if (type == AttrType::DATES) {
    // 允许字符串隐式转成日期，这样 WHERE u_date > '2020-1-20' 才能正确按日期比较
    return 2;
  }
  return INT32_MAX;
}

RC CharType::to_string(const Value &val, string &result) const
{
  stringstream ss;
  ss << val.value_.pointer_value_;
  result = ss.str();
  return RC::SUCCESS;
}