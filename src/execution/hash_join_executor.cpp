//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"

#include <utility>

#include "common/util/hash_util.h"
#include "type/value_factory.h"

namespace bustub {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_child)),
      right_executor_(std::move(right_child)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for Fall 2024: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

auto HashJoinExecutor::HashJoinKey::operator==(const HashJoinKey &other) const -> bool {
  if (values_.size() != other.values_.size()) {
    return false;
  }

  for (uint32_t i = 0; i < values_.size(); i++) {
    if (values_[i].CompareEquals(other.values_[i]) != CmpBool::CmpTrue) {
      return false;
    }
  }
  return true;
}

auto HashJoinExecutor::HashJoinKeyHasher::operator()(const HashJoinKey &key) const -> std::size_t {
  std::size_t hash = 0;
  for (const auto &value : key.values_) {
    hash = HashUtil::CombineHashes(hash, HashUtil::HashValue(&value));
  }
  return hash;
}

auto HashJoinExecutor::MakeJoinKey(const Tuple &tuple, const std::vector<AbstractExpressionRef> &key_expressions,
                                   const Schema &schema) const -> std::optional<HashJoinKey> {
  std::vector<Value> values;
  values.reserve(key_expressions.size());

  for (const auto &expression : key_expressions) {
    auto value = expression->Evaluate(&tuple, schema);
    if (value.IsNull()) {
      return std::nullopt;
    }
    values.emplace_back(std::move(value));
  }
  return HashJoinKey{std::move(values)};
}

auto HashJoinExecutor::MakeOutputTuple(const Tuple &left, const Tuple &right) const -> Tuple {
  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();

  std::vector<Value> values;
  values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());
  for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
    values.emplace_back(left.GetValue(&left_schema, i));
  }
  for (uint32_t i = 0; i < right_schema.GetColumnCount(); i++) {
    values.emplace_back(right.GetValue(&right_schema, i));
  }
  return Tuple{values, &GetOutputSchema()};
}

auto HashJoinExecutor::MakeLeftOuterTuple(const Tuple &left) const -> Tuple {
  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();

  std::vector<Value> values;
  values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());
  for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
    values.emplace_back(left.GetValue(&left_schema, i));
  }
  for (const auto &column : right_schema.GetColumns()) {
    values.emplace_back(ValueFactory::GetNullValueByType(column.GetType()));
  }
  return Tuple{values, &GetOutputSchema()};
}

void HashJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  hash_table_.clear();
  left_tuple_.reset();
  matches_.clear();
  match_cursor_ = 0;
  left_matched_ = false;

  Tuple right_tuple;
  RID right_rid;
  while (right_executor_->Next(&right_tuple, &right_rid)) {
    auto key = MakeJoinKey(right_tuple, plan_->RightJoinKeyExpressions(), right_executor_->GetOutputSchema());
    if (key.has_value()) {
      hash_table_[std::move(*key)].emplace_back(right_tuple);
    }
  }
}

auto HashJoinExecutor::Next(Tuple *tuple, [[maybe_unused]] RID *rid) -> bool {
  RID unused;
  while (true) {
    if (!left_tuple_.has_value()) {
      Tuple left_tuple;
      if (!left_executor_->Next(&left_tuple, &unused)) {
        return false;
      }

      left_tuple_ = std::move(left_tuple);
      matches_.clear();
      match_cursor_ = 0;
      left_matched_ = false;

      auto key = MakeJoinKey(*left_tuple_, plan_->LeftJoinKeyExpressions(), left_executor_->GetOutputSchema());
      if (key.has_value()) {
        auto matches = hash_table_.find(*key);
        if (matches != hash_table_.end()) {
          matches_ = matches->second;
        }
      }
    }

    if (match_cursor_ < matches_.size()) {
      *tuple = MakeOutputTuple(*left_tuple_, matches_[match_cursor_++]);
      left_matched_ = true;
      return true;
    }

    if (plan_->GetJoinType() == JoinType::LEFT && !left_matched_) {
      *tuple = MakeLeftOuterTuple(*left_tuple_);
      left_tuple_.reset();
      return true;
    }

    left_tuple_.reset();
  }
}

}  // namespace bustub
