//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2018-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/config.h"
#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set next page id and set max size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetMaxSize(max_size);
  SetPageType(IndexPageType::LEAF_PAGE);
  SetSize(0);
  SetNextPageId(INVALID_PAGE_ID);
  SetParentPage(INVALID_PAGE_ID);
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return next_page_id_; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

/*
 * Helper method to find and return the key associated with input "index" (a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::RidAt(int index) const -> ValueType { return rid_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAt(int index) -> bool {
  if (GetSize() == 0) {
    return false;
  }

  int size = GetSize();
  for (int i = index; i < size - 1; i++) {
    key_array_[i] = key_array_[i + 1];
    rid_array_[i] = rid_array_[i + 1];
  }

  SetSize(GetSize() - 1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveToAnother(int start_index, BPlusTreeLeafPage &other) {
  int size = GetSize();
  for (int i = start_index; i < size; i++) {
    other.key_array_[i - start_index] = std::move(key_array_[i]);
    other.rid_array_[i - start_index] = std::move(rid_array_[i]);
  }

  SetSize(GetSize() - (size - start_index));
  other.SetSize(size - start_index);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::InsertMapping(const KeyType &key, const ValueType &value, KeyComparator &comparator)
    -> bool {
  if (GetSize() == LEAF_PAGE_SLOT_CNT) {
    return false;
  }

  int index = SearchInsertPos(key, comparator);

  for (int i = GetSize(); i > index; i--) {
    key_array_[i] = key_array_[i - 1];
    rid_array_[i] = rid_array_[i - 1];
  }

  key_array_[index] = key;
  rid_array_[index] = value;

  SetSize(GetSize() + 1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::SearchInsertPos(const KeyType &key, KeyComparator &comparator) -> int {
  int left = 0;
  int right = GetSize() - 1;
  int mid;
  int ans = -1;
  while (left <= right) {
    mid = (left + right) / 2;
    if (comparator(KeyAt(mid), key) < 0) {
      ans = mid;
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }

  return ans + 1;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::BinarySearchForIndex(const KeyType &key, KeyComparator &comparator,
                                                      int &_out_index) const -> bool {
  int left = 0;
  int right = this->GetSize() - 1;
  int mid = -1;
  int cmp;
  while (left <= right) {
    mid = (left + right) / 2;
    cmp = comparator(this->KeyAt(mid), key);

    if (cmp == 0) {
      _out_index = mid;
      return true;
    }
    if (cmp < 0) {
      left = mid + 1;

    } else {
      right = mid - 1;
    }
  }

  return false;
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
