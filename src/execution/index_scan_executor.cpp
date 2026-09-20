//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/index_scan_executor.h"
#include <vector>
#include "catalog/schema.h"
#include "common/rid.h"
#include "concurrency/transaction_manager.h"
#include "execution/execution_common.h"

namespace bustub {
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan), dummy_schema_(Schema({})) {}

void IndexScanExecutor::Init() {
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
    if (!table_info_) {
        throw ExecutionException("table not found");
    }
    index_info_ = exec_ctx_->GetCatalog()->GetIndex(plan_->GetIndexOid());
    if (!index_info_) {
        throw ExecutionException("index not found");
    }
    tree_ = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get());
    if (tree_ == nullptr) {
        throw ExecutionException("unsupported index type");
    }
    cursor_ = 0;
    rids_.clear();

    ExecScan();
}

void IndexScanExecutor::ExecScan() {
    if (!plan_->pred_keys_.empty()) {
        std::unordered_set<RID> seen_rid;

        for (auto &key : plan_->pred_keys_) {
            std::vector<RID> matches;

            auto value = key->Evaluate(nullptr, dummy_schema_);

            Tuple key_tuple({value}, index_info_->index_->GetKeySchema());

            tree_->ScanKey(key_tuple, &matches, exec_ctx_->GetTransaction());

            for (auto &rid : matches) {
                if (seen_rid.insert(rid).second) {
                    rids_.push_back(rid);
                }
            }
        }
    } else {
        auto itor = tree_->GetBeginIterator();
        auto end = tree_->GetEndIterator();

        while (itor != end) {
            auto [key, rid] = *itor;
            rids_.push_back(rid);
            ++itor;
        }
    }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
    while (cursor_ < rids_.size()) {
        RID curr_rid = rids_[cursor_++];

        auto [meta_data, tuple_data, undo_link] =
            GetTupleAndUndoLink(exec_ctx_->GetTransactionManager(), table_info_->table_.get(), curr_rid);
        auto undo_logs = CollectUndoLogs(curr_rid, meta_data, tuple_data, undo_link, exec_ctx_->GetTransaction(),
                                         exec_ctx_->GetTransactionManager());
        if (!undo_logs.has_value()) {
            continue;
        }
        auto visible_tuple = ReconstructTuple(&table_info_->schema_, tuple_data, meta_data, *undo_logs);
        if (!visible_tuple.has_value()) {
            continue;
        }

        if (plan_->filter_predicate_ != nullptr) {
            auto value = plan_->filter_predicate_->Evaluate(&*visible_tuple, table_info_->schema_);

            if (value.IsNull() || !value.GetAs<bool>()) {
                continue;
            }
        }

        *tuple = std::move(*visible_tuple);
        tuple->SetRid(curr_rid);
        *rid = curr_rid;
        return true;
    }

    return false;
}

}  // namespace bustub
