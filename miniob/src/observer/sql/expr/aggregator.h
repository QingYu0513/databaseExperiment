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

#pragma once

#include "common/value.h"
#include "common/sys/rc.h"

/**
 * @brief 聚合器基类
 * @ingroup Expression
 * @details 火山模型下每条元组调用一次 accumulate() 累积状态，
 *          全部元组处理完后再调用 evaluate() 得到最终结果。
 *          每个 AggregateExpr 会创建一个独立的聚合器实例。
 */
class Aggregator
{
public:
  virtual ~Aggregator() = default;

  virtual RC accumulate(const Value &value) = 0;
  virtual RC evaluate(Value &result)        = 0;

protected:
  /// 计算过程中的中间值，同时承担"是否已初始化"的标记作用
  /// 默认构造的 Value 类型为 UNDEFINED，表示还没有收到任何元组
  Value value_;
};

/**
 * @brief SUM 聚合
 * @details 结果类型与入参一致（INT 求和得 INT，FLOAT 求和得 FLOAT）
 */
class SumAggregator : public Aggregator
{
public:
  RC accumulate(const Value &value) override;
  RC evaluate(Value &result) override;
};

/**
 * @brief COUNT 聚合
 * @details count(*) 与 count(字段) 都统计行数。结果类型固定为 INT。
 *          注意：MiniOB 当前没有 NULL 语义，因此 count(字段) 与 count(*) 等价。
 */
class CountAggregator : public Aggregator
{
public:
  RC accumulate(const Value &value) override;
  RC evaluate(Value &result) override;

private:
  int count_ = 0;
};

/**
 * @brief AVG 聚合
 * @details 内部以 float 累加，结果类型固定为 FLOAT（与 MySQL 行为一致：
 *          整数求平均也返回小数）。
 */
class AvgAggregator : public Aggregator
{
public:
  RC accumulate(const Value &value) override;
  RC evaluate(Value &result) override;

private:
  float sum_   = 0.0f;
  int   count_ = 0;
};

/**
 * @brief MAX 聚合
 * @details 结果类型与入参一致，支持 INT / FLOAT / CHAR / DATE，
 *          比较统一走 Value::compare，因此各类型的比较语义与 WHERE 条件完全一致。
 */
class MaxAggregator : public Aggregator
{
public:
  RC accumulate(const Value &value) override;
  RC evaluate(Value &result) override;
};

/**
 * @brief MIN 聚合
 * @details 与 MAX 对称。
 */
class MinAggregator : public Aggregator
{
public:
  RC accumulate(const Value &value) override;
  RC evaluate(Value &result) override;
};
