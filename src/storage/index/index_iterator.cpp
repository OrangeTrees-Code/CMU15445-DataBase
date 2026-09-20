/**
 * index_iterator.cpp
 */
#include <cassert>
#include <stdexcept>
#include "buffer/buffer_pool_manager.h"
#include "common/config.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page_guard.h"

#include "storage/index/index_iterator.h"

namespace bustub {

/*
 * NOTE: you can change the destructor/constructor method here
 * set your own input parameters
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator() = default;

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(BufferPoolManager *bpm, page_id_t leaf_page_id, int index)
    : bpm_(bpm), leaf_page_id_(leaf_page_id), index_(index){};

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() = default;  // NOLINT

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool { return this->leaf_page_id_ == INVALID_PAGE_ID; }

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  if (IsEnd()) {
    throw std::out_of_range("Cannot dereferance end iterator");
  }
  auto guard = bpm_->ReadPage(this->leaf_page_id_);
  auto leaf_page = guard.As<B_PLUS_TREE_LEAF_PAGE_TYPE>();

  current_key_ = leaf_page->KeyAt(index_);
  current_value_ = leaf_page->RidAt(index_);
  return {current_key_, current_value_};
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  if (IsEnd()) {
    return *this;
  }

  auto guard = bpm_->ReadPage(leaf_page_id_);
  auto leaf = guard.As<B_PLUS_TREE_LEAF_PAGE_TYPE>();

  index_++;

  if (index_ < leaf->GetSize()) {
    return *this;
  }

  leaf_page_id_ = leaf->GetNextPageId();
  index_ = leaf_page_id_ == INVALID_PAGE_ID ? -1 : 0;

  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
