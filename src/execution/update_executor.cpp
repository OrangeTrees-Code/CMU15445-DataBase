//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>
#include <vector>
#include "common/exception.h"
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"

#include "execution/execution_common.h"
#include "execution/executors/update_executor.h"

namespace bustub {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
    // As of Fall 2022, you DON'T need to implement update executor to have perfect score in project 3 / project 4.
}

void UpdateExecutor::Init() {
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid());
    if (!table_info_) {
        throw ExecutionException("table not found");
    }

    index_infos_ = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);

    has_returned_ = false;
    child_executor_->Init();
}

auto UpdateExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
    if (has_returned_) {
        return false;
    }

    struct PendingUpdate {
        Tuple old_tuple_;
        Tuple new_tuple_;
        RID rid_;
        bool primary_key_changed_{false};
    };

    std::shared_ptr<IndexInfo> primary_index = nullptr;
    for (const auto &index_info : index_infos_) {
        if (index_info->is_primary_key_) {
            primary_index = index_info;
            break;
        }
    }

    std::vector<PendingUpdate> pending;
    Tuple child_tuple;
    RID child_tuple_rid;

    // Updating a value used by the scan predicate must not make the iterator visit a tuple twice.
    // Fully consume the child before touching the table heap.
    while (child_executor_->Next(&child_tuple, &child_tuple_rid)) {
        std::vector<Value> values;
        values.reserve(plan_->target_expressions_.size());
        for (const auto &expr : plan_->target_expressions_) {
            values.push_back(expr->Evaluate(&child_tuple, child_executor_->GetOutputSchema()));
        }

        Tuple new_tuple(values, &table_info_->schema_);
        bool key_changed = false;
        if (primary_index != nullptr) {
            auto old_key = child_tuple.KeyFromTuple(table_info_->schema_, primary_index->key_schema_,
                                                    primary_index->index_->GetKeyAttrs());
            auto new_key = new_tuple.KeyFromTuple(table_info_->schema_, primary_index->key_schema_,
                                                  primary_index->index_->GetKeyAttrs());
            key_changed = !IsTupleContentEqual(old_key, new_key);
        }
        pending.push_back(PendingUpdate{child_tuple, std::move(new_tuple), child_tuple_rid, key_changed});
    }

    // A primary-key update is a delete followed by an insert. Delete the whole batch first so
    // transformations such as key = key + 1 can reuse the existing tombstones.
    for (const auto &entry : pending) {
        if (entry.primary_key_changed_) {
            ModifyTuple(&table_info_->schema_, table_info_->table_.get(), exec_ctx_->GetTransactionManager(),
                        exec_ctx_->GetTransaction(), table_info_->oid_, entry.rid_, nullptr);
        }
    }
    for (const auto &entry : pending) {
        if (!entry.primary_key_changed_) {
            ModifyTuple(&table_info_->schema_, table_info_->table_.get(), exec_ctx_->GetTransactionManager(),
                        exec_ctx_->GetTransaction(), table_info_->oid_, entry.rid_, &entry.new_tuple_);
        }
    }
    for (const auto &entry : pending) {
        if (entry.primary_key_changed_) {
            InsertTupleMvcc(table_info_, index_infos_, exec_ctx_->GetTransactionManager(), exec_ctx_->GetTransaction(),
                            entry.new_tuple_);
        }
    }

    *tuple = Tuple({Value(TypeId::INTEGER, static_cast<int32_t>(pending.size()))}, &GetOutputSchema());
    has_returned_ = true;
    return true;
}

}  // namespace bustub
