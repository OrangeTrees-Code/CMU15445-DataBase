//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.cpp
//
// Identification: src/execution/external_merge_sort_executor.cpp
//
// Copyright (c) 2015-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/external_merge_sort_executor.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace bustub {

template <size_t K>
ExternalMergeSortExecutor<K>::ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                                                        std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), cmp_(plan->GetOrderBy()), child_executor_(std::move(child_executor)) {}

template <size_t K>
ExternalMergeSortExecutor<K>::~ExternalMergeSortExecutor() {
    ClearResult();
}

template <size_t K>
void ExternalMergeSortExecutor<K>::Init() {
    ClearResult();
    child_executor_->Init();

    auto runs = CreateInitialRuns();
    while (runs.size() > 1) {
        std::vector<MergeSortRun> next_round;
        next_round.reserve((runs.size() + 1) / 2);

        size_t run_index = 0;
        for (; run_index + 1 < runs.size(); run_index += 2) {
            next_round.emplace_back(MergeRuns(runs[run_index], runs[run_index + 1]));
        }
        if (run_index < runs.size()) {
            next_round.emplace_back(std::move(runs[run_index]));
        }
        runs = std::move(next_round);
    }

    if (!runs.empty()) {
        result_run_.emplace(std::move(runs.front()));
        result_iterator_.emplace(result_run_->Begin());
    }
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(Tuple *tuple, RID *rid) -> bool {
    if (!result_iterator_.has_value() || !result_run_.has_value() || *result_iterator_ == result_run_->End()) {
        ClearResult();
        return false;
    }

    auto sorted_tuple = **result_iterator_;
    *tuple = sorted_tuple;
    *rid = sorted_tuple.GetRid();
    ++(*result_iterator_);
    return true;
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::CreateInitialRuns() -> std::vector<MergeSortRun> {
    const auto &schema = child_executor_->GetOutputSchema();
    if (!schema.IsInlined()) {
        throw NotImplementedException("external merge sort only supports fixed-length tuples");
    }

    const auto tuple_size = schema.GetInlinedStorageSize();
    const auto tuples_per_page = SortPage::GetMaxTupleCount(tuple_size);
    if (tuples_per_page == 0) {
        throw ExecutionException("tuple is too large for an external merge sort page");
    }

    std::vector<MergeSortRun> runs;
    std::vector<SortEntry> entries;
    entries.reserve(tuples_per_page);

    const auto flush_entries = [&]() {
        if (entries.empty()) {
            return;
        }

        std::sort(entries.begin(), entries.end(), cmp_);

        const auto page_id = exec_ctx_->GetBufferPoolManager()->NewPage();
        if (page_id == INVALID_PAGE_ID) {
            throw ExecutionException("could not allocate a temporary sort page");
        }

        {
            auto page_guard = exec_ctx_->GetBufferPoolManager()->WritePage(page_id);
            auto *page = page_guard.AsMut<SortPage>();
            page->Init(tuple_size);
            for (const auto &entry : entries) {
                if (!page->AppendTuple(entry.second)) {
                    throw ExecutionException("could not append a tuple to a temporary sort page");
                }
            }
        }

        runs.emplace_back(std::vector<page_id_t>{page_id}, exec_ctx_->GetBufferPoolManager());
        entries.clear();
    };

    Tuple child_tuple;
    RID child_rid;
    while (child_executor_->Next(&child_tuple, &child_rid)) {
        // The executor interface carries the RID independently from Tuple. Some executors (for
        // example ProjectionExecutor) construct a fresh Tuple, so persist the out-parameter here.
        child_tuple.SetRid(child_rid);
        entries.emplace_back(MakeSortEntry(child_tuple));
        if (entries.size() == tuples_per_page) {
            flush_entries();
        }
    }
    flush_entries();

    return runs;
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::MergeRuns(MergeSortRun &left, MergeSortRun &right) -> MergeSortRun {
    const auto tuple_size = GetOutputSchema().GetInlinedStorageSize();
    std::vector<page_id_t> output_pages;

    {
        std::optional<WritePageGuard> output_guard;
        const auto append_tuple = [&](const Tuple &tuple) {
            if (!output_guard.has_value() ||
                output_guard->As<SortPage>()->GetTupleCount() == SortPage::GetMaxTupleCount(tuple_size)) {
                output_guard.reset();
                const auto page_id = exec_ctx_->GetBufferPoolManager()->NewPage();
                if (page_id == INVALID_PAGE_ID) {
                    throw ExecutionException("could not allocate a temporary sort page");
                }
                output_guard.emplace(exec_ctx_->GetBufferPoolManager()->WritePage(page_id));
                output_guard->AsMut<SortPage>()->Init(tuple_size);
                output_pages.push_back(page_id);
            }

            if (!output_guard->AsMut<SortPage>()->AppendTuple(tuple)) {
                throw ExecutionException("could not append a tuple to a temporary sort page");
            }
        };

        auto left_it = left.Begin();
        auto left_end = left.End();
        auto right_it = right.Begin();
        auto right_end = right.End();

        while (left_it != left_end || right_it != right_end) {
            if (right_it == right_end) {
                auto tuple = *left_it;
                append_tuple(tuple);
                ++left_it;
            } else if (left_it == left_end) {
                auto tuple = *right_it;
                append_tuple(tuple);
                ++right_it;
            } else {
                auto left_entry = MakeSortEntry(*left_it);
                auto right_entry = MakeSortEntry(*right_it);
                if (cmp_(left_entry, right_entry)) {
                    append_tuple(left_entry.second);
                    ++left_it;
                } else {
                    append_tuple(right_entry.second);
                    ++right_it;
                }
            }
        }
    }

    // Both input iterators and the output page guard are gone now, so no page is pinned while we
    // reclaim the previous merge round.
    left.DeletePages();
    right.DeletePages();
    return {std::move(output_pages), exec_ctx_->GetBufferPoolManager()};
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::MakeSortEntry(const Tuple &tuple) const -> SortEntry {
    return {GenerateSortKey(tuple, plan_->GetOrderBy(), GetOutputSchema()), tuple};
}

template <size_t K>
void ExternalMergeSortExecutor<K>::ClearResult() {
    result_iterator_.reset();
    if (result_run_.has_value()) {
        result_run_->DeletePages();
        result_run_.reset();
    }
}

template class ExternalMergeSortExecutor<2>;

}  // namespace bustub
