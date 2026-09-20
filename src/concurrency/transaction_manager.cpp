//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// transaction_manager.cpp
//
// Identification: src/concurrency/transaction_manager.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/transaction_manager.h"

#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "catalog/catalog.h"
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "concurrency/transaction.h"
#include "execution/execution_common.h"
#include "storage/table/table_heap.h"
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

auto TransactionManager::Begin(IsolationLevel isolation_level) -> Transaction * {
    std::unique_lock<std::shared_mutex> l(txn_map_mutex_);
    auto txn_id = next_txn_id_++;
    auto txn = std::make_unique<Transaction>(txn_id, isolation_level);
    auto *txn_ref = txn.get();
    txn_ref->read_ts_ = last_commit_ts_.load();
    txn_map_.insert(std::make_pair(txn_id, std::move(txn)));

    running_txns_.AddTxn(txn_ref->read_ts_);
    return txn_ref;
}

auto TransactionManager::VerifyTxn(Transaction *txn) -> bool { return true; }

auto TransactionManager::Commit(Transaction *txn) -> bool {
    std::unique_lock<std::mutex> commit_lck(commit_mutex_);

    const timestamp_t commit_ts = last_commit_ts_.load() + 1;

    if (txn->state_ != TransactionState::RUNNING) {
        throw Exception("txn not in running state");
    }

    if (txn->GetIsolationLevel() == IsolationLevel::SERIALIZABLE) {
        if (!VerifyTxn(txn)) {
            commit_lck.unlock();
            Abort(txn);
            return false;
        }
    }

    for (const auto &[table_oid, rids] : txn->GetWriteSets()) {
        auto table_info = catalog_->GetTable(table_oid);
        BUSTUB_ASSERT(table_info != nullptr, "transaction write set refers to an unknown table");
        for (const auto &rid : rids) {
            auto meta = table_info->table_->GetTupleMeta(rid);
            if (meta.ts_ == txn->GetTransactionTempTs()) {
                table_info->table_->UpdateTupleMeta(TupleMeta{commit_ts, meta.is_deleted_}, rid);
            }
        }
    }

    std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);
    txn->commit_ts_ = commit_ts;
    txn->state_ = TransactionState::COMMITTED;
    last_commit_ts_.store(commit_ts);
    running_txns_.UpdateCommitTs(txn->commit_ts_);
    running_txns_.RemoveTxn(txn->read_ts_);

    return true;
}

void TransactionManager::Abort(Transaction *txn) {
    if (txn->state_ != TransactionState::RUNNING && txn->state_ != TransactionState::TAINTED) {
        throw Exception("txn not in running / tainted state");
    }

    // Physical rollback is intentionally left to optional Bonus Task #1. Required tasks only
    // transition the transaction and release its watermark entry.

    std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);
    txn->state_ = TransactionState::ABORTED;
    running_txns_.RemoveTxn(txn->read_ts_);
}

void TransactionManager::GarbageCollection() {
    const auto watermark = GetWatermark();
    std::unordered_set<txn_id_t> reachable_txns;

    for (const auto &table_name : catalog_->GetTableNames()) {
        auto table_info = catalog_->GetTable(table_name);
        for (auto iter = table_info->table_->MakeEagerIterator(); !iter.IsEnd(); ++iter) {
            auto [meta, tuple] = iter.GetTuple();
            static_cast<void>(tuple);
            if (meta.ts_ < TXN_START_ID && meta.ts_ <= watermark) {
                continue;
            }

            auto link = GetUndoLink(iter.GetRID());
            while (link.has_value() && link->IsValid()) {
                auto log = GetUndoLogOptional(*link);
                if (!log.has_value()) {
                    break;
                }
                reachable_txns.insert(link->prev_txn_);
                if (log->ts_ <= watermark) {
                    break;
                }
                link = log->prev_version_.IsValid() ? std::make_optional(log->prev_version_) : std::nullopt;
            }
        }
    }

    std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);
    for (auto iter = txn_map_.begin(); iter != txn_map_.end();) {
        const auto state = iter->second->GetTransactionState();
        const bool finished = state == TransactionState::COMMITTED || state == TransactionState::ABORTED;
        if (finished && reachable_txns.count(iter->first) == 0) {
            iter = txn_map_.erase(iter);
        } else {
            ++iter;
        }
    }
}

}  // namespace bustub
