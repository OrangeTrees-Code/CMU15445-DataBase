//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"

#include "type/value_factory.h"

namespace bustub {

NestIndexJoinExecutor::NestIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                             std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Spring: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestIndexJoinExecutor::Init() {
  child_executor_->Init();
  inner_table_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetInnerTableOid());
  index_info_ = exec_ctx_->GetCatalog()->GetIndex(plan_->GetIndexOid());
  outer_tuple_.reset();
  matches_.clear();
  match_cursor_ = 0;
  outer_matched_ = false;
}

auto NestIndexJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  RID unused;
  while (true) {
    if (!outer_tuple_.has_value()) {
      Tuple outer;
      if (!child_executor_->Next(&outer, &unused)) return false;
      outer_tuple_ = std::move(outer);
      matches_.clear();
      match_cursor_ = 0;
      outer_matched_ = false;
      auto key_value = plan_->KeyPredicate()->Evaluate(&*outer_tuple_, child_executor_->GetOutputSchema());
      Tuple key{{key_value}, index_info_->index_->GetKeySchema()};
      index_info_->index_->ScanKey(key, &matches_, exec_ctx_->GetTransaction());
    }
    if (match_cursor_ < matches_.size()) {
      auto [meta, inner] = inner_table_->table_->GetTuple(matches_[match_cursor_++]);
      if (meta.is_deleted_) continue;
      std::vector<Value> values;
      for (uint32_t i = 0; i < child_executor_->GetOutputSchema().GetColumnCount(); ++i) values.push_back(outer_tuple_->GetValue(&child_executor_->GetOutputSchema(), i));
      for (uint32_t i = 0; i < plan_->InnerTableSchema().GetColumnCount(); ++i) values.push_back(inner.GetValue(&plan_->InnerTableSchema(), i));
      *tuple = Tuple{values, &GetOutputSchema()};
      outer_matched_ = true;
      return true;
    }
    if (plan_->GetJoinType() == JoinType::LEFT && !outer_matched_) {
      std::vector<Value> values;
      for (uint32_t i = 0; i < child_executor_->GetOutputSchema().GetColumnCount(); ++i) values.push_back(outer_tuple_->GetValue(&child_executor_->GetOutputSchema(), i));
      for (const auto &column : plan_->InnerTableSchema().GetColumns()) values.push_back(ValueFactory::GetNullValueByType(column.GetType()));
      outer_tuple_.reset();
      *tuple = Tuple{values, &GetOutputSchema()};
      return true;
    }
    outer_tuple_.reset();
  }
}

}  // namespace bustub
