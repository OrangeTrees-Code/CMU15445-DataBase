//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"
#include <cstddef>
#include "common/exception.h"
#include "concurrency/transaction_manager.h"
#include "execution/execution_common.h"

namespace bustub {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
    auto table_oid = plan_->GetTableOid();
    table_info_ = exec_ctx_->GetCatalog()->GetTable(table_oid);

    if (!table_info_) {
        throw ExecutionException("table not found");
    }

    itor_.emplace(table_info_->table_->MakeIterator());
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
    while (itor_.has_value() && !itor_.value().IsEnd()) {
        auto tuple_rid = itor_.value().GetRID();
        ++itor_.value();

        auto [meta_data, tuple_data, undo_link] =
            GetTupleAndUndoLink(exec_ctx_->GetTransactionManager(), table_info_->table_.get(), tuple_rid);
        auto undo_logs = CollectUndoLogs(tuple_rid, meta_data, tuple_data, undo_link, exec_ctx_->GetTransaction(),
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
        tuple->SetRid(tuple_rid);
        *rid = tuple_rid;
        return true;
    }

    return false;
}

}  // namespace bustub
