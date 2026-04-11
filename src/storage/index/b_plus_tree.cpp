#include "storage/index/b_plus_tree.h"
#include <algorithm>
#include <optional>
#include "common/config.h"
#include "storage/index/b_plus_tree_debug.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_header_page.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      bpm_(buffer_pool_manager),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.template AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  std::shared_lock lock(tree_latch_);
  return IsEmptyUnlocked();
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmptyUnlocked() const -> bool {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto root_page = guard.As<BPlusTreeHeaderPage>();
  return root_page->root_page_id_ == INVALID_PAGE_ID;
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  std::shared_lock lock(tree_latch_);
  // Declaration of context instance.
  result->clear();

  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto header = header_guard.As<BPlusTreeHeaderPage>();

  if (header->root_page_id_ == INVALID_PAGE_ID) {
    return false;
  }

  page_id_t curr_page = header->root_page_id_;

  while (true) {
    auto data_guard = bpm_->ReadPage(curr_page);
    auto data = data_guard.As<BPlusTreePage>();

    if (data->IsLeafPage()) {
      auto leaf = data_guard.As<LeafPage>();
      if (int index = -1; leaf->BinarySearchForIndex(key, comparator_, index)) {
        result->push_back(leaf->RidAt(index));
        return true;
      }
      return false;
    }

    auto internal = data_guard.As<InternalPage>();

    curr_page = internal->ValueAt(internal->BinarySearchForArea(key, comparator_));
  }
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  std::unique_lock lock(tree_latch_);
  // Declaration of context instance.
  Context ctx;
  ctx.root_page_id_ = GetRootPageIdUnlocked();
  if (IsEmptyUnlocked()) {
    CreateNewRoot(key, value, ctx);
    return true;
  }

  FindLeaf(key, ctx);

  auto &leaf_guard = ctx.write_set_.back();
  auto leaf = leaf_guard.template AsMut<LeafPage>();

  if (int index; !(leaf->BinarySearchForIndex(key, comparator_, index))) {
    leaf->InsertMapping(key, value, comparator_);

    if (leaf->GetSize() <= leaf->GetMaxSize()) {
      return true;
    }

    auto [new_key, new_leaf] = SplitLeaf(leaf);

    InsertIntoParent(ctx, new_key, new_leaf);
    return true;
  }

  return false;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertIntoParent(Context &ctx, const KeyType &key, page_id_t page_id) {
  if (ctx.write_set_.size() == 1) {
    auto &old_page_guard = ctx.write_set_.back();

    page_id_t new_page_id = bpm_->NewPage();
    auto new_page_guard = bpm_->WritePage(new_page_id);
    auto new_root_page = new_page_guard.template AsMut<InternalPage>();

    new_root_page->Init(internal_max_size_);
    new_root_page->SetValueAt(0, old_page_guard.GetPageId());
    new_root_page->SetKeyAt(1, key);
    new_root_page->SetValueAt(1, page_id);
    new_root_page->SetSize(2);
    old_page_guard.template AsMut<BPlusTreePage>()->SetParentPage(new_page_id);

    auto new_child_guard = bpm_->WritePage(page_id);
    new_child_guard.template AsMut<BPlusTreePage>()->SetParentPage(new_page_id);

    ctx.header_page_ = std::make_optional(bpm_->WritePage(header_page_id_));
    auto header_page = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
    header_page->root_page_id_ = new_page_id;
    ctx.header_page_ = std::nullopt;

    return;
  }

  ctx.write_set_.pop_back();

  auto &parent_guard = ctx.write_set_.back();
  auto parent_page = parent_guard.template AsMut<InternalPage>();

  parent_page->InsertMapping(key, page_id, comparator_);

  if (parent_page->GetSize() <= parent_page->GetMaxSize()) {
    auto new_child_guard = bpm_->WritePage(page_id);
    new_child_guard.template AsMut<BPlusTreePage>()->SetParentPage(parent_guard.GetPageId());
    return;
  }

  auto [new_key, new_page_id] = SplitInternal(parent_page);

  InsertIntoParent(ctx, new_key, new_page_id);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::SplitInternal(InternalPage *internal_page) -> std::pair<KeyType, page_id_t> {
  int total = internal_page->GetSize();
  int mid = total / 2;

  KeyType up_key = internal_page->KeyAt(mid);

  page_id_t new_page_id = bpm_->NewPage();
  auto guard = bpm_->WritePage(new_page_id);
  auto new_page = guard.template AsMut<InternalPage>();

  new_page->Init(internal_max_size_);
  new_page->SetParentPage(internal_page->GetParentPage());

  int new_size = total - mid;

  new_page->SetValueAt(0, internal_page->ValueAt(mid));
  for (int i = 1; i < new_size; i++) {
    new_page->SetKeyAt(i, internal_page->KeyAt(mid + i));
    new_page->SetValueAt(i, internal_page->ValueAt(mid + i));
  }
  new_page->SetSize(new_size);
  internal_page->SetSize(mid);

  for (int i = 0; i < new_size; i++) {
    auto child_guard = bpm_->WritePage(new_page->ValueAt(i));
    child_guard.template AsMut<BPlusTreePage>()->SetParentPage(new_page_id);
  }

  return {up_key, new_page_id};
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::SplitLeaf(LeafPage *leaf_page) -> std::pair<KeyType, page_id_t> {
  page_id_t new_page_id = bpm_->NewPage();
  auto guard = bpm_->WritePage(new_page_id);
  auto new_page = guard.template AsMut<LeafPage>();

  new_page->Init(leaf_max_size_);
  new_page->SetParentPage(leaf_page->GetParentPage());

  int move_start = leaf_page->GetSize() / 2;

  leaf_page->MoveToAnother(move_start, *new_page);

  new_page->SetNextPageId(leaf_page->GetNextPageId());

  leaf_page->SetNextPageId(new_page_id);

  return std::pair<KeyType, page_id_t>{new_page->KeyAt(0), new_page_id};
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeaf(const KeyType &key, Context &ctx) -> page_id_t {
  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto header = header_guard.As<BPlusTreeHeaderPage>();

  auto curr_page = header->root_page_id_;
  while (true) {
    auto guard = bpm_->WritePage(curr_page);
    auto page = guard.template AsMut<BPlusTreePage>();
    if (page->IsLeafPage()) {
      ctx.write_set_.push_back(std::move(guard));
      return curr_page;
    }

    auto internal = guard.template AsMut<InternalPage>();
    auto next_page = internal->ValueAt(internal->BinarySearchForArea(key, comparator_));
    ctx.write_set_.push_back(std::move(guard));
    curr_page = next_page;
  }
}

INDEX_TEMPLATE_ARGUMENTS void BPLUSTREE_TYPE::CreateNewRoot(const KeyType &key, const ValueType &value, Context &ctx) {
  page_id_t new_page = bpm_->NewPage();
  auto guard = bpm_->WritePage(new_page);
  auto root_page = guard.template AsMut<LeafPage>();
  root_page->Init(leaf_max_size_);
  root_page->InsertMapping(key, value, comparator_);

  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  auto header_page = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
  header_page->root_page_id_ = new_page;
  ctx.header_page_ = std::nullopt;
  ctx.root_page_id_ = new_page;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  std::unique_lock lock(tree_latch_);
  if (IsEmptyUnlocked()) {
    return;
  }

  Context ctx;
  ctx.root_page_id_ = GetRootPageIdUnlocked();
  FindLeaf(key, ctx);
  auto &leaf_guard = ctx.write_set_.back();
  auto leaf_page = leaf_guard.AsMut<LeafPage>();

  if (int index; leaf_page->BinarySearchForIndex(key, comparator_, index)) {
    leaf_page->RemoveAt(index);

    if (ctx.write_set_.size() == 1) {
      AdjustRootAfterDelete(ctx);
      return;
    }

    if (index == 0 && leaf_page->GetSize() > 0) {
      UpdateAncestorsAfterMinKeyChange(ctx);
    }

    if (leaf_page->GetSize() >= leaf_page->GetMinSize()) {
      return;
    }

    CoalesceOrRedistribute(ctx);
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::CoalesceOrRedistribute(Context &ctx) -> bool {
  auto &curr_page_guard = ctx.write_set_.back();
  auto curr_page = curr_page_guard.AsMut<BPlusTreePage>();

  if (curr_page->GetSize() >= curr_page->GetMinSize()) {
    return true;
  }

  auto &parent_guard = ctx.write_set_[ctx.write_set_.size() - 2];
  auto parent = parent_guard.AsMut<InternalPage>();
  int curr_index = parent->ValueIndex(curr_page_guard.GetPageId());
  if (curr_index < 0) {
    return true;
  }

  if (curr_page->IsLeafPage()) {
    if (RedistributeLeaf(ctx, parent, curr_index)) {
      return true;
    }

    CoalesceLeaf(ctx, parent, curr_index);
  } else {
    if (RedistributeInternal(ctx, parent, curr_index)) {
      return true;
    }

    CoalesceInternal(ctx, parent, curr_index);
  }

  ctx.write_set_.pop_back();
  if (ctx.write_set_.size() == 1) {
    AdjustRootAfterDelete(ctx);
    return true;
  }

  return CoalesceOrRedistribute(ctx);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveChildFromParent(InternalPage *parent, int remove_index) {
  int parent_size = parent->GetSize();
  for (int i = remove_index; i < parent_size - 1; i++) {
    parent->SetValueAt(i, parent->ValueAt(i + 1));
  }
  for (int i = std::max(1, remove_index); i < parent_size - 1; i++) {
    parent->SetKeyAt(i, parent->KeyAt(i + 1));
  }
  parent->SetSize(parent_size - 1);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateChildParent(page_id_t child_page_id, page_id_t new_parent_id) {
  auto child_guard = bpm_->WritePage(child_page_id);
  child_guard.template AsMut<BPlusTreePage>()->SetParentPage(new_parent_id);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::RedistributeLeaf(Context &ctx, InternalPage *parent, int curr_index) -> bool {
  auto &curr_guard = ctx.write_set_.back();
  auto curr_leaf = curr_guard.template AsMut<LeafPage>();

  if (curr_index > 0) {
    page_id_t left_page_id = parent->ValueAt(curr_index - 1);
    if (left_page_id == curr_guard.GetPageId()) {
      return false;
    }
    auto left_guard = bpm_->WritePage(left_page_id);
    auto left_leaf = left_guard.template AsMut<LeafPage>();
    if (left_leaf->GetSize() > left_leaf->GetMinSize()) {
      int last_index = left_leaf->GetSize() - 1;
      curr_leaf->InsertMapping(left_leaf->KeyAt(last_index), left_leaf->RidAt(last_index), comparator_);
      left_leaf->RemoveAt(last_index);
      parent->SetKeyAt(curr_index, curr_leaf->KeyAt(0));
      return true;
    }
  }

  if (curr_index + 1 < parent->GetSize()) {
    page_id_t right_page_id = parent->ValueAt(curr_index + 1);
    if (right_page_id == curr_guard.GetPageId()) {
      return false;
    }
    auto right_guard = bpm_->WritePage(right_page_id);
    auto right_leaf = right_guard.template AsMut<LeafPage>();
    if (right_leaf->GetSize() > right_leaf->GetMinSize()) {
      curr_leaf->InsertMapping(right_leaf->KeyAt(0), right_leaf->RidAt(0), comparator_);
      right_leaf->RemoveAt(0);
      parent->SetKeyAt(curr_index + 1, right_leaf->KeyAt(0));
      return true;
    }
  }

  return false;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::CoalesceLeaf(Context &ctx, InternalPage *parent, int curr_index) {
  auto &curr_guard = ctx.write_set_.back();
  auto curr_leaf = curr_guard.template AsMut<LeafPage>();

  if (curr_index > 0) {
    page_id_t left_page_id = parent->ValueAt(curr_index - 1);
    if (left_page_id == curr_guard.GetPageId()) {
      return;
    }
    auto left_guard = bpm_->WritePage(left_page_id);
    auto left_leaf = left_guard.template AsMut<LeafPage>();
    int curr_size = curr_leaf->GetSize();
    for (int i = 0; i < curr_size; i++) {
      left_leaf->InsertMapping(curr_leaf->KeyAt(i), curr_leaf->RidAt(i), comparator_);
    }
    left_leaf->SetNextPageId(curr_leaf->GetNextPageId());
    RemoveChildFromParent(parent, curr_index);
    bpm_->DeletePage(curr_guard.GetPageId());
    return;
  }

  page_id_t right_page_id = parent->ValueAt(curr_index + 1);
  if (right_page_id == curr_guard.GetPageId()) {
    return;
  }
  auto right_guard = bpm_->WritePage(right_page_id);
  auto right_leaf = right_guard.template AsMut<LeafPage>();
  int right_size = right_leaf->GetSize();
  for (int i = 0; i < right_size; i++) {
    curr_leaf->InsertMapping(right_leaf->KeyAt(i), right_leaf->RidAt(i), comparator_);
  }
  curr_leaf->SetNextPageId(right_leaf->GetNextPageId());
  RemoveChildFromParent(parent, curr_index + 1);
  bpm_->DeletePage(right_guard.GetPageId());
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::RedistributeInternal(Context &ctx, InternalPage *parent, int curr_index) -> bool {
  auto &curr_guard = ctx.write_set_.back();
  auto curr_internal = curr_guard.template AsMut<InternalPage>();

  if (curr_index > 0) {
    page_id_t left_page_id = parent->ValueAt(curr_index - 1);
    if (left_page_id == curr_guard.GetPageId()) {
      return false;
    }
    auto left_guard = bpm_->WritePage(left_page_id);
    auto left_internal = left_guard.template AsMut<InternalPage>();
    if (left_internal->GetSize() > left_internal->GetMinSize()) {
      int curr_size = curr_internal->GetSize();
      for (int i = curr_size; i > 0; i--) {
        curr_internal->SetValueAt(i, curr_internal->ValueAt(i - 1));
      }
      for (int i = curr_size; i > 1; i--) {
        curr_internal->SetKeyAt(i, curr_internal->KeyAt(i - 1));
      }
      curr_internal->SetValueAt(0, left_internal->ValueAt(left_internal->GetSize() - 1));
      curr_internal->SetKeyAt(1, parent->KeyAt(curr_index));
      curr_internal->SetSize(curr_size + 1);

      parent->SetKeyAt(curr_index, left_internal->KeyAt(left_internal->GetSize() - 1));
      left_internal->SetSize(left_internal->GetSize() - 1);
      UpdateChildParent(curr_internal->ValueAt(0), curr_guard.GetPageId());
      return true;
    }
  }

  if (curr_index + 1 < parent->GetSize()) {
    page_id_t right_page_id = parent->ValueAt(curr_index + 1);
    if (right_page_id == curr_guard.GetPageId()) {
      return false;
    }
    auto right_guard = bpm_->WritePage(right_page_id);
    auto right_internal = right_guard.template AsMut<InternalPage>();
    if (right_internal->GetSize() > right_internal->GetMinSize()) {
      int curr_size = curr_internal->GetSize();
      curr_internal->SetKeyAt(curr_size, parent->KeyAt(curr_index + 1));
      curr_internal->SetValueAt(curr_size, right_internal->ValueAt(0));
      curr_internal->SetSize(curr_size + 1);

      parent->SetKeyAt(curr_index + 1, right_internal->KeyAt(1));
      int right_size = right_internal->GetSize();
      for (int i = 0; i < right_size - 1; i++) {
        right_internal->SetValueAt(i, right_internal->ValueAt(i + 1));
      }
      for (int i = 1; i < right_size - 1; i++) {
        right_internal->SetKeyAt(i, right_internal->KeyAt(i + 1));
      }
      right_internal->SetSize(right_size - 1);
      UpdateChildParent(curr_internal->ValueAt(curr_size), curr_guard.GetPageId());
      return true;
    }
  }

  return false;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::CoalesceInternal(Context &ctx, InternalPage *parent, int curr_index) {
  auto &curr_guard = ctx.write_set_.back();
  auto curr_internal = curr_guard.template AsMut<InternalPage>();

  if (curr_index > 0) {
    page_id_t left_page_id = parent->ValueAt(curr_index - 1);
    if (left_page_id == curr_guard.GetPageId()) {
      return;
    }
    auto left_guard = bpm_->WritePage(left_page_id);
    auto left_internal = left_guard.template AsMut<InternalPage>();
    int left_size = left_internal->GetSize();
    left_internal->SetKeyAt(left_size, parent->KeyAt(curr_index));
    left_internal->SetValueAt(left_size, curr_internal->ValueAt(0));
    for (int i = 1; i < curr_internal->GetSize(); i++) {
      left_internal->SetKeyAt(left_size + i, curr_internal->KeyAt(i));
      left_internal->SetValueAt(left_size + i, curr_internal->ValueAt(i));
    }
    left_internal->SetSize(left_size + curr_internal->GetSize());
    for (int i = left_size; i < left_internal->GetSize(); i++) {
      UpdateChildParent(left_internal->ValueAt(i), left_guard.GetPageId());
    }
    RemoveChildFromParent(parent, curr_index);
    bpm_->DeletePage(curr_guard.GetPageId());
    return;
  }

  page_id_t right_page_id = parent->ValueAt(curr_index + 1);
  if (right_page_id == curr_guard.GetPageId()) {
    return;
  }
  auto right_guard = bpm_->WritePage(right_page_id);
  auto right_internal = right_guard.template AsMut<InternalPage>();
  int curr_size = curr_internal->GetSize();
  curr_internal->SetKeyAt(curr_size, parent->KeyAt(curr_index + 1));
  curr_internal->SetValueAt(curr_size, right_internal->ValueAt(0));
  for (int i = 1; i < right_internal->GetSize(); i++) {
    curr_internal->SetKeyAt(curr_size + i, right_internal->KeyAt(i));
    curr_internal->SetValueAt(curr_size + i, right_internal->ValueAt(i));
  }
  curr_internal->SetSize(curr_size + right_internal->GetSize());
  for (int i = curr_size; i < curr_internal->GetSize(); i++) {
    UpdateChildParent(curr_internal->ValueAt(i), curr_guard.GetPageId());
  }
  RemoveChildFromParent(parent, curr_index + 1);
  bpm_->DeletePage(right_guard.GetPageId());
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::AdjustRootAfterDelete(Context &ctx) {
  auto &guard = ctx.write_set_.back();
  auto root_page = guard.AsMut<BPlusTreePage>();

  auto replace_root_page_to = [&](page_id_t new_root_page_id) -> void {
    ctx.header_page_ = bpm_->WritePage(header_page_id_);
    auto header_page = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
    page_id_t old_root_page_id = header_page->root_page_id_;
    header_page->root_page_id_ = new_root_page_id;
    ctx.root_page_id_ = new_root_page_id;
    ctx.header_page_ = std::nullopt;
    bpm_->DeletePage(old_root_page_id);
  };

  if (root_page->IsLeafPage()) {
    if (root_page->GetSize() == 0) {
      replace_root_page_to(INVALID_PAGE_ID);
    }
    return;
  }

  auto internal = guard.AsMut<InternalPage>();
  if (internal->GetSize() == 1) {
    page_id_t new_root_page_id = internal->ValueAt(0);
    auto new_root_guard = bpm_->WritePage(new_root_page_id);
    new_root_guard.template AsMut<BPlusTreePage>()->SetParentPage(INVALID_PAGE_ID);

    replace_root_page_to(new_root_page_id);
  }
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateAncestorsAfterMinKeyChange(Context &ctx) {
  auto min_key = ctx.write_set_.back().template AsMut<LeafPage>()->KeyAt(0);
  for (int level = static_cast<int>(ctx.write_set_.size()) - 1; level > 0; level--) {
    auto &child_guard = ctx.write_set_[level];
    auto &parent_guard = ctx.write_set_[level - 1];
    auto parent = parent_guard.template AsMut<InternalPage>();
    int child_index = parent->ValueIndex(child_guard.GetPageId());
    if (child_index > 0) {
      parent->SetKeyAt(child_index, min_key);
      return;
    }
  }
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  std::shared_lock lock(tree_latch_);
  if (IsEmptyUnlocked()) {
    return End();
  }
  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto header_page = header_guard.As<BPlusTreeHeaderPage>();

  page_id_t curr_page_id = header_page->root_page_id_;

  while (true) {
    auto curr_guard = bpm_->ReadPage(curr_page_id);
    auto curr_page = curr_guard.As<BPlusTreePage>();

    if (curr_page->IsLeafPage()) {
      break;
    }

    auto internal = curr_guard.As<InternalPage>();

    curr_page_id = internal->ValueAt(0);
  }
  return INDEXITERATOR_TYPE(bpm_, curr_page_id, 0);
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  std::shared_lock lock(tree_latch_);
  if (IsEmptyUnlocked()) {
    return End();
  }

  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto header_page = header_guard.As<BPlusTreeHeaderPage>();

  page_id_t curr_page_id = header_page->root_page_id_;

  while (true) {
    auto curr_guard = bpm_->ReadPage(curr_page_id);
    auto curr_page = curr_guard.As<BPlusTreePage>();

    if (curr_page->IsLeafPage()) {
      auto leaf_page = curr_guard.As<LeafPage>();
      if (int index; leaf_page->BinarySearchForIndex(key, comparator_, index)) {
        return INDEXITERATOR_TYPE(bpm_, curr_page_id, index);
      }
      return End();
    }

    auto internal_page = curr_guard.As<InternalPage>();

    curr_page_id = internal_page->ValueAt(internal_page->BinarySearchForArea(key, comparator_));
  }
  return End();
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(bpm_, INVALID_PAGE_ID, -1); }

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  std::shared_lock lock(tree_latch_);
  return GetRootPageIdUnlocked();
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageIdUnlocked() -> page_id_t {
  if (header_page_id_ == INVALID_PAGE_ID) {
    return INVALID_PAGE_ID;
  }
  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto header = header_guard.As<BPlusTreeHeaderPage>();
  return header->root_page_id_;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
