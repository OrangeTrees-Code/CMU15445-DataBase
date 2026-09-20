//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.h
//
// Identification: src/include/execution/executors/hash_join_executor.h
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * HashJoinExecutor executes a hash JOIN on two tables.
 */
class HashJoinExecutor : public AbstractExecutor {
 public:
  /**
   * Construct a new HashJoinExecutor instance.
   * @param exec_ctx The executor context
   * @param plan The HashJoin join plan to be executed
   * @param left_child The child executor that produces tuples for the left side of join
   * @param right_child The child executor that produces tuples for the right side of join
   */
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child);

  /** Initialize the join */
  void Init() override;

  /**
   * Yield the next tuple from the join.
   * @param[out] tuple The next tuple produced by the join.
   * @param[out] rid The next tuple RID, not used by hash join.
   * @return `true` if a tuple was produced, `false` if there are no more tuples.
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the join */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); };

 private:
  /** A composite key made from the values of all join-key expressions. */
  struct HashJoinKey {
    std::vector<Value> values_;

    auto operator==(const HashJoinKey &other) const -> bool;
  };

  /** Hashes a composite join key while preserving its expression order. */
  struct HashJoinKeyHasher {
    auto operator()(const HashJoinKey &key) const -> std::size_t;
  };

  /**
   * Evaluates every join-key expression for a tuple.
   *
   * A NULL key cannot match an equi-join predicate, so it is represented by
   * std::nullopt rather than inserted/probed in the hash table.
   */
  auto MakeJoinKey(const Tuple &tuple, const std::vector<AbstractExpressionRef> &key_expressions,
                   const Schema &schema) const -> std::optional<HashJoinKey>;

  /** Builds one ordinary joined output tuple. */
  auto MakeOutputTuple(const Tuple &left, const Tuple &right) const -> Tuple;

  /** Builds the unmatched-left output tuple for a LEFT JOIN. */
  auto MakeLeftOuterTuple(const Tuple &left) const -> Tuple;

  /** The HashJoin plan node to be executed. */
  const HashJoinPlanNode *plan_;
  std::unique_ptr<AbstractExecutor> left_executor_;
  std::unique_ptr<AbstractExecutor> right_executor_;
  std::unordered_map<HashJoinKey, std::vector<Tuple>, HashJoinKeyHasher> hash_table_;
  std::optional<Tuple> left_tuple_;
  std::vector<Tuple> matches_;
  size_t match_cursor_{0};
  bool left_matched_{false};
};

}  // namespace bustub
