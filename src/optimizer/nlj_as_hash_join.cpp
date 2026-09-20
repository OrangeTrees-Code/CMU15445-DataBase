#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

namespace {

/** The key expressions needed to replace one nested-loop join predicate. */
struct HashJoinKeys {
    std::vector<AbstractExpressionRef> left_keys_;
    std::vector<AbstractExpressionRef> right_keys_;
};

/**
 * Extract hash join keys from a predicate consisting solely of equality comparisons
 * between a left-child column and a right-child column. AND expressions are flattened
 * recursively so every equality becomes one key component.
 */
auto ExtractHashJoinKeys(const AbstractExpressionRef &predicate) -> std::optional<HashJoinKeys> {
    if (predicate == nullptr) {
        return std::nullopt;
    }

    if (const auto *logic = dynamic_cast<const LogicExpression *>(predicate.get()); logic != nullptr) {
        if (logic->logic_type_ != LogicType::And) {
            return std::nullopt;
        }

        auto left = ExtractHashJoinKeys(logic->GetChildAt(0));
        auto right = ExtractHashJoinKeys(logic->GetChildAt(1));
        if (!left.has_value() || !right.has_value()) {
            return std::nullopt;
        }

        left->left_keys_.insert(left->left_keys_.end(), right->left_keys_.begin(), right->left_keys_.end());
        left->right_keys_.insert(left->right_keys_.end(), right->right_keys_.begin(), right->right_keys_.end());
        return left;
    }

    const auto *comparison = dynamic_cast<const ComparisonExpression *>(predicate.get());
    if (comparison == nullptr || comparison->comp_type_ != ComparisonType::Equal) {
        return std::nullopt;
    }

    const auto &lhs = comparison->GetChildAt(0);
    const auto &rhs = comparison->GetChildAt(1);
    const auto *left_column = dynamic_cast<const ColumnValueExpression *>(lhs.get());
    const auto *right_column = dynamic_cast<const ColumnValueExpression *>(rhs.get());
    if (left_column == nullptr || right_column == nullptr) {
        return std::nullopt;
    }

    if (left_column->GetTupleIdx() == 0 && right_column->GetTupleIdx() == 1) {
        return HashJoinKeys{{lhs}, {rhs}};
    }
    if (left_column->GetTupleIdx() == 1 && right_column->GetTupleIdx() == 0) {
        return HashJoinKeys{{rhs}, {lhs}};
    }
    return std::nullopt;
}

}  // namespace

auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
    std::vector<AbstractPlanNodeRef> children;
    children.reserve(plan->GetChildren().size());
    for (const auto &child : plan->GetChildren()) {
        children.emplace_back(OptimizeNLJAsHashJoin(child));
    }
    auto optimized_plan = plan->CloneWithChildren(std::move(children));

    if (optimized_plan->GetType() != PlanType::NestedLoopJoin) {
        return optimized_plan;
    }

    const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);
    if (nlj_plan.GetChildren().size() != 2) {
        return optimized_plan;
    }

    auto keys = ExtractHashJoinKeys(nlj_plan.Predicate());
    if (!keys.has_value()) {
        return optimized_plan;
    }

    return std::make_shared<HashJoinPlanNode>(nlj_plan.output_schema_, nlj_plan.GetLeftPlan(), nlj_plan.GetRightPlan(),
                                              std::move(keys->left_keys_), std::move(keys->right_keys_),
                                              nlj_plan.GetJoinType());
}

}  // namespace bustub
