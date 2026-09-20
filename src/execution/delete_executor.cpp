//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/rid.h"
#include "storage/table/tuple.h"

#include "execution/execution_common.h"
#include "execution/executors/delete_executor.h"

namespace bustub {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
    table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid());
    if (!table_info_) {
        throw ExecutionException("table not found");
    }
    index_infos_ = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);
    has_returned_ = false;
    child_executor_->Init();
}

auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
    if (has_returned_) {
        return false;
    }

    int deleted_count = 0;
    Tuple child_tuple;
    RID child_tuple_rid;

    while (child_executor_->Next(&child_tuple, &child_tuple_rid)) {
        ModifyTuple(&table_info_->schema_, table_info_->table_.get(), exec_ctx_->GetTransactionManager(),
                    exec_ctx_->GetTransaction(), table_info_->oid_, child_tuple_rid, nullptr);
        deleted_count++;
    }

    *tuple = Tuple({Value(TypeId::INTEGER, deleted_count)}, &GetOutputSchema());
    has_returned_ = true;
    return true;
}

}  // namespace bustub
