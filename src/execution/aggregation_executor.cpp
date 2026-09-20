//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>
#include <vector>

#include "execution/executors/aggregation_executor.h"

namespace bustub {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      aht_(plan->GetAggregates(), plan->GetAggregateTypes()) {}

void AggregationExecutor::Init() {
  aht_.Clear();
  child_executor_->Init();

  Tuple child_tuple;
  RID child_rid;
  bool has_input = false;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    has_input = true;
    aht_.InsertCombine(MakeAggregateKey(&child_tuple), MakeAggregateValue(&child_tuple));
  }

  if (!has_input && plan_->GetGroupBys().empty()) {
    aht_.InsertInitial(AggregateKey{});
  }
  aht_iterator_.emplace(aht_.Begin());
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (!aht_iterator_.has_value() || *aht_iterator_ == aht_.End()) {
    return false;
  }

  const auto &key = aht_iterator_->Key();
  const auto &value = aht_iterator_->Val();
  std::vector<Value> values;
  values.reserve(key.group_bys_.size() + value.aggregates_.size());
  values.insert(values.end(), key.group_bys_.begin(), key.group_bys_.end());
  values.insert(values.end(), value.aggregates_.begin(), value.aggregates_.end());
  *tuple = Tuple{values, &GetOutputSchema()};
  ++(*aht_iterator_);
  return true;
}

auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
