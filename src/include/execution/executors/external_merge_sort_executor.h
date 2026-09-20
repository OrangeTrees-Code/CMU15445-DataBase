//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.h
//
// Identification: src/include/execution/executors/external_merge_sort_executor.h
//
// Copyright (c) 2015-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "buffer/buffer_pool_manager.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/execution_common.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/sort_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * Page to hold the intermediate data for external merge sort.
 *
 * Only fixed-length data will be supported in Fall 2024.
 */
class SortPage {
   public:
    /**
     * Initialize an empty sort page. All tuples in a sort page have the same length because Project
     * 3 only requires sorting fixed-length tuples.
     */
    void Init(uint32_t tuple_size) {
        if (GetMaxTupleCount(tuple_size) == 0) {
            throw ExecutionException("tuple is too large for an external merge sort page");
        }
        tuple_count_ = 0;
        tuple_size_ = tuple_size;
    }

    /** @return the number of tuples currently stored in this page. */
    auto GetTupleCount() const -> uint32_t { return tuple_count_; }

    /** @return the fixed serialized size of one tuple in this page. */
    auto GetTupleSize() const -> uint32_t { return tuple_size_; }

    /** @return the maximum number of tuples of the specified size that fit in one sort page. */
    static auto GetMaxTupleCount(uint32_t tuple_size) -> uint32_t {
        const auto entry_size = static_cast<size_t>(tuple_size) + sizeof(int64_t);
        return entry_size == 0 ? 0 : static_cast<uint32_t>((BUSTUB_PAGE_SIZE - kHeaderSize) / entry_size);
    }

    /**
     * Append a tuple to this page.
     *
     * In addition to the tuple payload, retain its RID. A SortPlanNode does not change tuples, so
     * preserving the RID makes this executor behave like other tuple-at-a-time executors.
     */
    auto AppendTuple(const Tuple &tuple) -> bool {
        if (tuple.GetLength() != tuple_size_ || tuple_count_ >= GetMaxTupleCount(tuple_size_)) {
            return false;
        }

        auto *entry = GetEntry(tuple_count_);
        const int64_t encoded_rid = tuple.GetRid().Get();
        std::memcpy(entry, &encoded_rid, sizeof(encoded_rid));
        std::memcpy(entry + sizeof(encoded_rid), tuple.GetData(), tuple_size_);
        tuple_count_++;
        return true;
    }

    /** Return a deep copy of the tuple at the given position. */
    auto GetTuple(uint32_t index) const -> Tuple {
        BUSTUB_ASSERT(index < tuple_count_, "sort page tuple index out of bounds");

        const auto *entry = GetEntry(index);
        int64_t encoded_rid;
        std::memcpy(&encoded_rid, entry, sizeof(encoded_rid));
        return {RID{encoded_rid}, entry + sizeof(encoded_rid), tuple_size_};
    }

   private:
    /**
     * Page layout:
     *
     * | tuple count (4) | tuple size (4) | RID (8) | tuple bytes | ... |
     *
     * Storing raw fixed-size tuple bytes keeps the layout compact. The RID is kept separately
     * because Tuple::SerializeTo intentionally serializes only tuple data.
     */
    static constexpr size_t kHeaderSize = sizeof(uint32_t) * 2;

    auto GetEntry(uint32_t index) -> char * {
        const auto entry_size = static_cast<size_t>(tuple_size_) + sizeof(int64_t);
        return reinterpret_cast<char *>(this) + kHeaderSize + index * entry_size;
    }

    auto GetEntry(uint32_t index) const -> const char * {
        const auto entry_size = static_cast<size_t>(tuple_size_) + sizeof(int64_t);
        return reinterpret_cast<const char *>(this) + kHeaderSize + index * entry_size;
    }

    uint32_t tuple_count_;
    uint32_t tuple_size_;
};

static_assert(sizeof(SortPage) == sizeof(uint32_t) * 2);

/**
 * A data structure that holds the sorted tuples as a run during external merge sort.
 * Tuples might be stored in multiple pages, and tuples are ordered both within one page
 * and across pages.
 */
class MergeSortRun {
   public:
    MergeSortRun() = default;
    MergeSortRun(std::vector<page_id_t> pages, BufferPoolManager *bpm) : pages_(std::move(pages)), bpm_(bpm) {}

    auto GetPageCount() const -> size_t { return pages_.size(); }

    /** Release every temporary page owned by this run. */
    void DeletePages() {
        for (const auto page_id : pages_) {
            if (page_id != INVALID_PAGE_ID) {
                bpm_->DeletePage(page_id);
            }
        }
        pages_.clear();
    }

    /** Iterator for iterating on the sorted tuples in one run. */
    class Iterator {
        friend class MergeSortRun;

       public:
        Iterator() = default;
        Iterator(const Iterator &other)
            : run_(other.run_), page_index_(other.page_index_), tuple_index_(other.tuple_index_) {}
        auto operator=(const Iterator &other) -> Iterator & {
            if (this != &other) {
                page_guard_.reset();
                run_ = other.run_;
                page_index_ = other.page_index_;
                tuple_index_ = other.tuple_index_;
            }
            return *this;
        }
        Iterator(Iterator &&) noexcept = default;
        auto operator=(Iterator &&) noexcept -> Iterator & = default;

        /**
         * Advance the iterator to the next tuple. If the current sort page is exhausted, move to the
         * next sort page.
         */
        auto operator++() -> Iterator & {
            AdvanceToTuple();
            if (IsEnd()) {
                return *this;
            }

            tuple_index_++;
            const auto *page = page_guard_->As<SortPage>();
            if (tuple_index_ == page->GetTupleCount()) {
                page_guard_.reset();
                page_index_++;
                tuple_index_ = 0;
                AdvanceToTuple();
            }
            return *this;
        }

        /**
         * Dereference the iterator to get the current tuple in the sorted run that the iterator is
         * pointing to.
         */
        auto operator*() -> Tuple {
            AdvanceToTuple();
            BUSTUB_ASSERT(!IsEnd(), "cannot dereference the end of a merge sort run");
            return page_guard_->As<SortPage>()->GetTuple(tuple_index_);
        }

        /**
         * Checks whether two iterators are pointing to the same tuple in the same sorted run.
         */
        auto operator==(const Iterator &other) const -> bool {
            return run_ == other.run_ && page_index_ == other.page_index_ && tuple_index_ == other.tuple_index_;
        }

        /**
         * Checks whether two iterators are pointing to different tuples in a sorted run or iterating
         * on different sorted runs.
         */
        auto operator!=(const Iterator &other) const -> bool { return !(*this == other); }

       private:
        explicit Iterator(const MergeSortRun *run, size_t page_index = 0, uint32_t tuple_index = 0)
            : run_(run), page_index_(page_index), tuple_index_(tuple_index) {
            AdvanceToTuple();
        }

        auto IsEnd() const -> bool { return run_ == nullptr || page_index_ >= run_->pages_.size(); }

        /** Pin the current page, skipping any empty pages defensively. */
        void AdvanceToTuple() {
            while (!IsEnd()) {
                if (!page_guard_.has_value()) {
                    page_guard_.emplace(run_->bpm_->ReadPage(run_->pages_[page_index_]));
                }
                const auto *page = page_guard_->As<SortPage>();
                if (tuple_index_ < page->GetTupleCount()) {
                    return;
                }
                page_guard_.reset();
                page_index_++;
                tuple_index_ = 0;
            }
        }

        /** The sorted run that the iterator is iterating on. */
        const MergeSortRun *run_{nullptr};

        /**
         * The iterator pins only its current input page. This bounds a two-way merge to two input
         * pages plus one output page, while still avoiding a buffer-pool lookup for every tuple.
         */
        size_t page_index_{0};
        uint32_t tuple_index_{0};
        std::optional<ReadPageGuard> page_guard_;
    };

    /**
     * Get an iterator pointing to the beginning of the sorted run, i.e. the first tuple.
     */
    auto Begin() -> Iterator { return Iterator{this}; }

    /**
     * Get an iterator pointing to the end of the sorted run, i.e. the position after the last tuple.
     */
    auto End() -> Iterator { return Iterator{this, pages_.size()}; }

   private:
    /** The page IDs of the sort pages that store the sorted tuples. */
    std::vector<page_id_t> pages_;
    /**
     * The buffer pool manager used to read sort pages. The buffer pool manager is responsible for
     * deleting the sort pages when they are no longer needed.
     */
    [[maybe_unused]] BufferPoolManager *bpm_;
};

/**
 * ExternalMergeSortExecutor executes an external merge sort.
 *
 * In Fall 2024, only 2-way external merge sort is required.
 */
template <size_t K>
class ExternalMergeSortExecutor : public AbstractExecutor {
   public:
    ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                              std::unique_ptr<AbstractExecutor> &&child_executor);
    ~ExternalMergeSortExecutor() override;

    /** Initialize the external merge sort */
    void Init() override;

    /**
     * Yield the next tuple from the external merge sort.
     * @param[out] tuple The next tuple produced by the external merge sort.
     * @param[out] rid The next tuple RID produced by the external merge sort.
     * @return `true` if a tuple was produced, `false` if there are no more tuples
     */
    auto Next(Tuple *tuple, RID *rid) -> bool override;

    /** @return The output schema for the external merge sort */
    auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

   private:
    /** Materialize sorted, one-page initial runs from the child executor. */
    auto CreateInitialRuns() -> std::vector<MergeSortRun>;

    /** Merge two sorted runs into a new sorted run and release both input runs. */
    auto MergeRuns(MergeSortRun &left, MergeSortRun &right) -> MergeSortRun;

    /** Build the entry used by TupleComparator for one tuple. */
    auto MakeSortEntry(const Tuple &tuple) const -> SortEntry;

    /** Release the final run and any page guard held by its output iterator. */
    void ClearResult();

    /** The sort plan node to be executed */
    const SortPlanNode *plan_;

    /** Compares tuples based on the order-bys */
    TupleComparator cmp_;

    /** Child tuples are materialized into sorted temporary runs during Init(). */
    std::unique_ptr<AbstractExecutor> child_executor_;

    /** The final sorted run and its current output position. */
    std::optional<MergeSortRun> result_run_;
    std::optional<MergeSortRun::Iterator> result_iterator_;
};

}  // namespace bustub
