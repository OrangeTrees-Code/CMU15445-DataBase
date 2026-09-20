//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// execution_common.cpp
//
// Identification: src/execution/execution_common.cpp
//
// Copyright (c) 2024-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/execution_common.h"

#include <optional>
#include <utility>
#include <vector>

#include "catalog/catalog.h"
#include "common/exception.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "fmt/core.h"
#include "storage/table/table_heap.h"

namespace bustub {

namespace {

auto GetUndoLogSchema(const Schema *schema, const std::vector<bool> &modified_fields) -> Schema {
    std::vector<uint32_t> attrs;
    attrs.reserve(schema->GetColumnCount());
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
        if (modified_fields[i]) {
            attrs.push_back(i);
        }
    }
    return Schema::CopySchema(schema, attrs);
}

auto ValuesDiffer(const Schema *schema, const Tuple &lhs, const Tuple &rhs, uint32_t column) -> bool {
    return !lhs.GetValue(schema, column).CompareExactlyEquals(rhs.GetValue(schema, column));
}

[[noreturn]] void ThrowWriteConflict(Transaction *txn) {
    txn->SetTainted();
    throw ExecutionException("write-write conflict");
}

}  // namespace

TupleComparator::TupleComparator(std::vector<OrderBy> order_bys) : order_bys_(std::move(order_bys)) {}

auto TupleComparator::operator()(const SortEntry &entry_a, const SortEntry &entry_b) const -> bool {
    BUSTUB_ASSERT(entry_a.first.size() == order_bys_.size(), "sort entry has the wrong number of keys");
    BUSTUB_ASSERT(entry_b.first.size() == order_bys_.size(), "sort entry has the wrong number of keys");

    for (size_t i = 0; i < order_bys_.size(); i++) {
        const auto &lhs = entry_a.first[i];
        const auto &rhs = entry_b.first[i];
        const bool descending = order_bys_[i].first == OrderByType::DESC;

        if (lhs.IsNull() || rhs.IsNull()) {
            if (lhs.IsNull() && rhs.IsNull()) {
                continue;
            }
            // Match PostgreSQL's default ORDER BY behavior: NULLS LAST for ascending order and
            // NULLS FIRST for descending order. The project tests use fixed-length values, but this
            // also keeps the comparator a strict weak ordering for nullable keys.
            return lhs.IsNull() ? descending : !descending;
        }

        if (lhs.CompareLessThan(rhs) == CmpBool::CmpTrue) {
            return !descending;
        }
        if (lhs.CompareGreaterThan(rhs) == CmpBool::CmpTrue) {
            return descending;
        }
    }
    return false;
}

auto GenerateSortKey(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortKey {
    SortKey key;
    key.reserve(order_bys.size());
    for (const auto &order_by : order_bys) {
        key.emplace_back(order_by.second->Evaluate(&tuple, schema));
    }
    return key;
}

/**
 * Above are all you need for P3.
 * You can ignore the remaining part of this file until P4.
 */

/**
 * @brief Reconstruct a tuple by applying the provided undo logs from the base tuple. All logs in the undo_logs are
 * applied regardless of the timestamp
 *
 * @param schema The schema of the base tuple and the returned tuple.
 * @param base_tuple The base tuple to start the reconstruction from.
 * @param base_meta The metadata of the base tuple.
 * @param undo_logs The list of undo logs to apply during the reconstruction, the front is applied first.
 * @return An optional tuple that represents the reconstructed tuple. If the tuple is deleted as the result, returns
 * std::nullopt.
 */
auto ReconstructTuple(const Schema *schema, const Tuple &base_tuple, const TupleMeta &base_meta,
                      const std::vector<UndoLog> &undo_logs) -> std::optional<Tuple> {
    std::vector<Value> values;
    values.reserve(schema->GetColumnCount());
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
        values.push_back(base_tuple.GetValue(schema, i));
    }

    bool is_deleted = base_meta.is_deleted_;
    for (const auto &log : undo_logs) {
        BUSTUB_ASSERT(log.modified_fields_.size() == schema->GetColumnCount(), "undo log has an invalid field bitmap");
        auto undo_schema = GetUndoLogSchema(schema, log.modified_fields_);
        uint32_t undo_column = 0;
        for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
            if (log.modified_fields_[i]) {
                values[i] = log.tuple_.GetValue(&undo_schema, undo_column++);
            }
        }
        is_deleted = log.is_deleted_;
    }

    if (is_deleted) {
        return std::nullopt;
    }
    Tuple result{values, schema};
    result.SetRid(base_tuple.GetRid());
    return result;
}

/**
 * @brief Collects the undo logs sufficient to reconstruct the tuple w.r.t. the txn.
 *
 * @param rid The RID of the tuple.
 * @param base_meta The metadata of the base tuple.
 * @param base_tuple The base tuple.
 * @param undo_link The undo link to the latest undo log.
 * @param txn The transaction.
 * @param txn_mgr The transaction manager.
 * @return An optional vector of undo logs to pass to ReconstructTuple(). std::nullopt if the tuple did not exist at the
 * time.
 */
auto CollectUndoLogs(RID rid, const TupleMeta &base_meta, const Tuple &base_tuple, std::optional<UndoLink> undo_link,
                     Transaction *txn, TransactionManager *txn_mgr) -> std::optional<std::vector<UndoLog>> {
    static_cast<void>(rid);
    static_cast<void>(base_tuple);

    const auto read_ts = txn->GetReadTs();
    if (base_meta.ts_ == txn->GetTransactionTempTs() || (base_meta.ts_ < TXN_START_ID && base_meta.ts_ <= read_ts)) {
        return std::vector<UndoLog>{};
    }

    std::vector<UndoLog> logs;
    auto link = undo_link;
    while (link.has_value() && link->IsValid()) {
        auto log = txn_mgr->GetUndoLogOptional(*link);
        if (!log.has_value()) {
            BUSTUB_ASSERT(false, "visible version chain points to a garbage-collected undo log");
            return std::nullopt;
        }
        logs.push_back(*log);
        if (log->ts_ <= read_ts) {
            return logs;
        }
        link = log->prev_version_.IsValid() ? std::make_optional(log->prev_version_) : std::nullopt;
    }
    return std::nullopt;
}

/**
 * @brief Generates a new undo log as the transaction tries to modify this tuple at the first time.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param ts The timestamp of the base tuple.
 * @param prev_version The undo link to the latest undo log of this tuple.
 * @return The generated undo log.
 */
auto GenerateNewUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple, timestamp_t ts,
                        UndoLink prev_version) -> UndoLog {
    std::vector<bool> modified_fields(schema->GetColumnCount(), false);
    std::vector<Value> old_values;

    if (base_tuple != nullptr) {
        old_values.reserve(schema->GetColumnCount());
        for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
            const bool modified = target_tuple == nullptr || ValuesDiffer(schema, *base_tuple, *target_tuple, i);
            modified_fields[i] = modified;
            if (modified) {
                old_values.push_back(base_tuple->GetValue(schema, i));
            }
        }
    }

    auto undo_schema = GetUndoLogSchema(schema, modified_fields);
    return UndoLog{base_tuple == nullptr, std::move(modified_fields), Tuple{old_values, &undo_schema}, ts,
                   prev_version};
}

/**
 * @brief Generate the updated undo log to replace the old one, whereas the tuple is already modified by this txn once.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param log The original undo log.
 * @return The updated undo log.
 */
auto GenerateUpdatedUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple,
                            const UndoLog &log) -> UndoLog {
    BUSTUB_ASSERT(log.modified_fields_.size() == schema->GetColumnCount(), "undo log has an invalid field bitmap");

    // If the tuple did not exist before this transaction, no column values are needed to undo it.
    if (log.is_deleted_) {
        return log;
    }

    auto old_undo_schema = GetUndoLogSchema(schema, log.modified_fields_);
    std::vector<std::optional<Value>> original_values(schema->GetColumnCount());
    uint32_t undo_column = 0;
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
        if (log.modified_fields_[i]) {
            original_values[i] = log.tuple_.GetValue(&old_undo_schema, undo_column++);
        }
    }

    auto modified_fields = log.modified_fields_;
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
        const bool changed_now =
            target_tuple == nullptr || base_tuple == nullptr || ValuesDiffer(schema, *base_tuple, *target_tuple, i);
        if (changed_now && !modified_fields[i]) {
            BUSTUB_ASSERT(base_tuple != nullptr, "an existing tuple's original value must be available");
            modified_fields[i] = true;
            original_values[i] = base_tuple->GetValue(schema, i);
        }
    }

    std::vector<Value> values;
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
        if (modified_fields[i]) {
            BUSTUB_ASSERT(original_values[i].has_value(), "missing original value in consolidated undo log");
            values.push_back(*original_values[i]);
        }
    }
    auto undo_schema = GetUndoLogSchema(schema, modified_fields);
    return UndoLog{false, std::move(modified_fields), Tuple{values, &undo_schema}, log.ts_, log.prev_version_};
}

auto IsWriteWriteConflict(const TupleMeta &meta, Transaction *txn) -> bool {
    if (meta.ts_ == txn->GetTransactionTempTs()) {
        return false;
    }
    return meta.ts_ >= TXN_START_ID || meta.ts_ > txn->GetReadTs();
}

void ModifyTuple(const Schema *schema, TableHeap *table_heap, TransactionManager *txn_mgr, Transaction *txn,
                 table_oid_t table_oid, RID rid, const Tuple *target_tuple) {
    auto [base_meta, base_tuple, undo_link] = GetTupleAndUndoLink(txn_mgr, table_heap, rid);
    if (IsWriteWriteConflict(base_meta, txn)) {
        ThrowWriteConflict(txn);
    }

    const bool self_modify = base_meta.ts_ == txn->GetTransactionTempTs();
    auto new_link = undo_link;
    if (self_modify) {
        if (undo_link.has_value() && undo_link->prev_txn_ == txn->GetTransactionId()) {
            auto log = txn->GetUndoLog(undo_link->prev_log_idx_);
            auto updated_log =
                GenerateUpdatedUndoLog(schema, base_meta.is_deleted_ ? nullptr : &base_tuple, target_tuple, log);
            txn->ModifyUndoLog(undo_link->prev_log_idx_, std::move(updated_log));
        }
    } else {
        auto log = GenerateNewUndoLog(schema, base_meta.is_deleted_ ? nullptr : &base_tuple, target_tuple,
                                      base_meta.ts_, undo_link.value_or(UndoLink{}));
        new_link = txn->AppendUndoLog(std::move(log));
    }

    const Tuple &stored_tuple = target_tuple == nullptr ? base_tuple : *target_tuple;
    const TupleMeta new_meta{txn->GetTransactionTempTs(), target_tuple == nullptr};
    const auto expected_meta = base_meta;
    const auto expected_tuple = base_tuple;
    const bool updated = UpdateTupleAndUndoLink(
        txn_mgr, rid, new_link, table_heap, txn, new_meta, stored_tuple,
        [expected_meta, expected_tuple, txn](const TupleMeta &current_meta, const Tuple &current_tuple, RID,
                                             std::optional<UndoLink>) {
            return current_meta == expected_meta && IsTupleContentEqual(current_tuple, expected_tuple) &&
                   !IsWriteWriteConflict(current_meta, txn);
        });
    if (!updated) {
        ThrowWriteConflict(txn);
    }
    txn->AppendWriteSet(table_oid, rid);
}

void InsertTupleMvcc(const std::shared_ptr<TableInfo> &table_info,
                     const std::vector<std::shared_ptr<IndexInfo>> &index_infos, TransactionManager *txn_mgr,
                     Transaction *txn, const Tuple &tuple) {
    std::shared_ptr<IndexInfo> primary_index = nullptr;
    for (const auto &index_info : index_infos) {
        if (index_info->is_primary_key_) {
            primary_index = index_info;
            break;
        }
    }

    Tuple key;
    if (primary_index != nullptr) {
        key = tuple.KeyFromTuple(table_info->schema_, primary_index->key_schema_, primary_index->index_->GetKeyAttrs());
        std::vector<RID> matches;
        primary_index->index_->ScanKey(key, &matches, txn);
        if (!matches.empty()) {
            BUSTUB_ASSERT(matches.size() == 1, "a primary-key index returned duplicate RIDs");
            auto [meta, old_tuple, undo_link] = GetTupleAndUndoLink(txn_mgr, table_info->table_.get(), matches.front());
            static_cast<void>(old_tuple);
            static_cast<void>(undo_link);
            if (!meta.is_deleted_) {
                txn->SetTainted();
                throw ExecutionException("duplicate primary key");
            }
            ModifyTuple(&table_info->schema_, table_info->table_.get(), txn_mgr, txn, table_info->oid_, matches.front(),
                        &tuple);
            return;
        }
    }

    auto rid = table_info->table_->InsertTuple(TupleMeta{txn->GetTransactionTempTs(), false}, tuple);
    if (!rid.has_value()) {
        throw ExecutionException("failed to insert tuple into table heap");
    }
    txn->AppendWriteSet(table_info->oid_, *rid);

    if (primary_index != nullptr && !primary_index->index_->InsertEntry(key, *rid, txn)) {
        txn->SetTainted();
        throw ExecutionException("duplicate primary key");
    }
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
    fmt::println(stderr, "debug_hook: {}", info);
    const auto *schema = &table_info->schema_;
    for (auto iter = table_heap->MakeEagerIterator(); !iter.IsEnd(); ++iter) {
        const auto rid = iter.GetRID();
        auto [meta, tuple] = iter.GetTuple();
        const auto timestamp =
            meta.ts_ >= TXN_START_ID ? fmt::format("txn{}", meta.ts_ ^ TXN_START_ID) : fmt::format("{}", meta.ts_);
        fmt::println(stderr, "RID={}/{} ts={}{} tuple={}", rid.GetPageId(), rid.GetSlotNum(), timestamp,
                     meta.is_deleted_ ? " <del marker>" : "", tuple.ToString(schema));

        auto link = txn_mgr->GetUndoLink(rid);
        while (link.has_value() && link->IsValid()) {
            auto log = txn_mgr->GetUndoLogOptional(*link);
            if (!log.has_value()) {
                fmt::println(stderr, "  txn{}@{} <garbage collected>", link->prev_txn_ ^ TXN_START_ID,
                             link->prev_log_idx_);
                break;
            }
            if (log->is_deleted_) {
                fmt::println(stderr, "  txn{}@{} <del> ts={}", link->prev_txn_ ^ TXN_START_ID, link->prev_log_idx_,
                             log->ts_);
            } else {
                auto undo_schema = GetUndoLogSchema(schema, log->modified_fields_);
                fmt::println(stderr, "  txn{}@{} {} ts={}", link->prev_txn_ ^ TXN_START_ID, link->prev_log_idx_,
                             log->tuple_.ToString(&undo_schema), log->ts_);
            }
            link = log->prev_version_.IsValid() ? std::make_optional(log->prev_version_) : std::nullopt;
        }
    }
}

}  // namespace bustub
