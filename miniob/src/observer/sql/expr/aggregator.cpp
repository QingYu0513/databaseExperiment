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
// Created by Wangyunlai on 2024/05/29.
//

#include "sql/expr/aggregator.h"

#include <stdint.h>

#include "common/log/log.h"

///////////////////////////////////////////////////////////////////////////////
// SUM
RC SumAggregator::accumulate(const Value &value)
{
  if (value_.attr_type() == AttrType::UNDEFINED) {
    value_ = value;
    return RC::SUCCESS;
  }

  ASSERT(value.attr_type() == value_.attr_type(),
      "type mismatch. value type: %s, value_.type: %s",
      attr_type_to_string(value.attr_type()),
      attr_type_to_string(value_.attr_type()));

  Value::add(value, value_, value_);
  return RC::SUCCESS;
}

RC SumAggregator::evaluate(Value &result)
{
  result = value_;
  return RC::SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// COUNT
RC CountAggregator::accumulate(const Value &value)
{
  // count(*) 在 binder 阶段会被替换成常量表达式，这里只需计数即可。
  // 不使用 value_ 保存状态，但要维持"已初始化"的语义以便与其它聚合器保持一致。
  if (value_.attr_type() == AttrType::UNDEFINED) {
    value_ = value;
  }
  count_++;
  return RC::SUCCESS;
}

RC CountAggregator::evaluate(Value &result)
{
  result.set_int(count_);
  return RC::SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// AVG
RC AvgAggregator::accumulate(const Value &value)
{
  if (value_.attr_type() == AttrType::UNDEFINED) {
    value_ = value;
  }

  // get_float() 内部已经处理了 INT / FLOAT / CHAR 等类型的转换
  sum_ += value.get_float();
  count_++;
  return RC::SUCCESS;
}

RC AvgAggregator::evaluate(Value &result)
{
  if (count_ == 0) {
    // 没有任何元组时返回 0，避免除零
    result.set_float(0.0f);
    return RC::SUCCESS;
  }
  result.set_float(sum_ / static_cast<float>(count_));
  return RC::SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// MAX
RC MaxAggregator::accumulate(const Value &value)
{
  if (value_.attr_type() == AttrType::UNDEFINED) {
    value_ = value;
    return RC::SUCCESS;
  }

  const int cmp = value_.compare(value);
  if (cmp == INT32_MAX) {
    // 两个值无法比较（类型不兼容）
    LOG_WARN("failed to compare values in max aggregate");
    return RC::INVALID_ARGUMENT;
  }
  if (cmp < 0) {
    value_ = value;
  }
  return RC::SUCCESS;
}

RC MaxAggregator::evaluate(Value &result)
{
  result = value_;
  return RC::SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// MIN
RC MinAggregator::accumulate(const Value &value)
{
  if (value_.attr_type() == AttrType::UNDEFINED) {
    value_ = value;
    return RC::SUCCESS;
  }

  const int cmp = value_.compare(value);
  if (cmp == INT32_MAX) {
    LOG_WARN("failed to compare values in min aggregate");
    return RC::INVALID_ARGUMENT;
  }
  if (cmp > 0) {
    value_ = value;
  }
  return RC::SUCCESS;
}

RC MinAggregator::evaluate(Value &result)
{
  result = value_;
  return RC::SUCCESS;
}
