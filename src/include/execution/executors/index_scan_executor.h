//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.h
//
// Identification: src/include/execution/executors/index_scan_executor.h
//
// Copyright (c) 2015-20, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <unordered_set>
#include <vector>

#include "catalog/catalog.h"
#include "catalog/schema.h"
#include "common/rid.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/index_scan_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * IndexScanExecutor executes an index scan over a table.
 */

class IndexScanExecutor : public AbstractExecutor {
   public:
    /**
     * Creates a new index scan executor.
     * @param exec_ctx the executor context
     * @param plan the index scan plan to be executed
     */
    IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan);

    auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

    void Init() override;

    auto Next(Tuple *tuple, RID *rid) -> bool override;

   private:
    void ExecScan();

   private:
    /** The index scan plan node to be executed. */
    const IndexScanPlanNode *plan_;

    // std::shared_ptr<TableInfo> table_info_;

    std::shared_ptr<IndexInfo> index_info_;

    std::shared_ptr<TableInfo> table_info_;

    BPlusTreeIndexForTwoIntegerColumn *tree_;
    std::vector<RID> rids_;

    size_t cursor_;

    Schema dummy_schema_;
};
}  // namespace bustub
