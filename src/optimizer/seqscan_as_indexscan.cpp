#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/seq_scan_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

namespace {

struct PointLookup {
  uint32_t column_idx_;
  std::vector<AbstractExpressionRef> keys_;
};

auto ExtractPointLookup(const AbstractExpressionRef &expr) -> std::optional<PointLookup> {
  if (const auto *comparison = dynamic_cast<const ComparisonExpression *>(expr.get()); comparison != nullptr) {
    if (comparison->comp_type_ != ComparisonType::Equal) {
      return std::nullopt;
    }

    const auto &left = comparison->GetChildAt(0);
    const auto &right = comparison->GetChildAt(1);
    const auto *left_column = dynamic_cast<const ColumnValueExpression *>(left.get());
    const auto *right_column = dynamic_cast<const ColumnValueExpression *>(right.get());
    const auto *left_constant = dynamic_cast<const ConstantValueExpression *>(left.get());
    const auto *right_constant = dynamic_cast<const ConstantValueExpression *>(right.get());

    if (left_column != nullptr && left_column->GetTupleIdx() == 0 && right_constant != nullptr) {
      return PointLookup{left_column->GetColIdx(), {right}};
    }
    if (right_column != nullptr && right_column->GetTupleIdx() == 0 && left_constant != nullptr) {
      return PointLookup{right_column->GetColIdx(), {left}};
    }
    return std::nullopt;
  }

  const auto *logic = dynamic_cast<const LogicExpression *>(expr.get());
  if (logic == nullptr || logic->logic_type_ != LogicType::Or) {
    return std::nullopt;
  }

  auto left = ExtractPointLookup(logic->GetChildAt(0));
  auto right = ExtractPointLookup(logic->GetChildAt(1));
  if (!left.has_value() || !right.has_value() || left->column_idx_ != right->column_idx_) {
    return std::nullopt;
  }

  left->keys_.insert(left->keys_.end(), right->keys_.begin(), right->keys_.end());
  return left;
}

}  // namespace

auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;
  children.reserve(plan->GetChildren().size());
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSeqScanAsIndexScan(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() != PlanType::SeqScan) {
    return optimized_plan;
  }

  const auto &seq_scan = dynamic_cast<const SeqScanPlanNode &>(*optimized_plan);
  if (seq_scan.filter_predicate_ == nullptr) {
    return optimized_plan;
  }

  auto lookup = ExtractPointLookup(seq_scan.filter_predicate_);
  if (!lookup.has_value()) {
    return optimized_plan;
  }

  auto index = MatchIndex(seq_scan.table_name_, lookup->column_idx_);
  if (!index.has_value()) {
    return optimized_plan;
  }

  auto [index_oid, index_name] = *index;
  return std::make_shared<IndexScanPlanNode>(seq_scan.output_schema_, seq_scan.table_oid_, index_oid,
                                             seq_scan.filter_predicate_, std::move(lookup->keys_));
}

}  // namespace bustub
