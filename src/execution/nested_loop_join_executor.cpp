//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "common/exception.h"
#include "type/value_factory.h"

namespace bustub {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Fall: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  left_tuple_.reset();
  left_matched_ = false;
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  Tuple right_tuple;
  RID unused_rid;
  while (true) {
    if (!left_tuple_.has_value()) {
      Tuple next_left;
      if (!left_executor_->Next(&next_left, &unused_rid)) {
        return false;
      }
      left_tuple_ = std::move(next_left);
      left_matched_ = false;
      right_executor_->Init();
    }

    while (right_executor_->Next(&right_tuple, &unused_rid)) {
      auto value = plan_->Predicate()->EvaluateJoin(&*left_tuple_, left_executor_->GetOutputSchema(), &right_tuple,
                                                    right_executor_->GetOutputSchema());
      if (value.IsNull() || !value.GetAs<bool>()) {
        continue;
      }
      left_matched_ = true;
      std::vector<Value> values;
      for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); ++i) {
        values.push_back(left_tuple_->GetValue(&left_executor_->GetOutputSchema(), i));
      }
      for (uint32_t i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); ++i) {
        values.push_back(right_tuple.GetValue(&right_executor_->GetOutputSchema(), i));
      }
      *tuple = Tuple{values, &GetOutputSchema()};
      return true;
    }

    if (plan_->GetJoinType() == JoinType::LEFT && !left_matched_) {
      std::vector<Value> values;
      for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); ++i) {
        values.push_back(left_tuple_->GetValue(&left_executor_->GetOutputSchema(), i));
      }
      for (const auto &column : right_executor_->GetOutputSchema().GetColumns()) {
        values.push_back(ValueFactory::GetNullValueByType(column.GetType()));
      }
      *tuple = Tuple{values, &GetOutputSchema()};
      left_tuple_.reset();
      return true;
    }
    left_tuple_.reset();
  }
}

}  // namespace bustub
