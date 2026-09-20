//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>
#include "common/rid.h"
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"

#include "execution/execution_common.h"
#include "execution/executors/insert_executor.h"

namespace bustub {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
    auto catalog = exec_ctx_->GetCatalog();
    table_info_ = catalog->GetTable(plan_->GetTableOid());

    if (!table_info_) {
        throw ExecutionException("table not found");
    }

    index_infos_ = catalog->GetTableIndexes(table_info_->name_);

    has_returned_ = false;

    child_executor_->Init();
}

auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
    if (has_returned_) {
        return false;
    }

    Tuple child_tuple;
    RID child_tuple_rid;

    int inserted_count = 0;

    while (child_executor_->Next(&child_tuple, &child_tuple_rid)) {
        InsertTupleMvcc(table_info_, index_infos_, exec_ctx_->GetTransactionManager(), exec_ctx_->GetTransaction(),
                        child_tuple);
        ++inserted_count;
    }

    *tuple = Tuple({Value(TypeId::INTEGER, inserted_count)}, &GetOutputSchema());
    has_returned_ = true;
    return true;
}

}  // namespace bustub
