//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include "common/config.h"
#include "common/exception.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::ChooseEvictFrame() -> frame_id_t {
  std::vector<frame_id_t> inf_frames;

  frame_id_t evict_fid = -1;
  size_t max_k_distance = 0;
  size_t k_distance = 0;

  bool has_inf_distance = false;

  auto get_oldest = [&](frame_id_t fid) -> size_t { return node_store_.at(fid).history_.front(); };

  auto compute_max_distance = [&](LRUKNode &node) -> size_t { return current_timestamp_ - node.history_.front(); };

  for (auto &[fid, node] : node_store_) {
    if (node.is_evictable_) {
      if (node.history_.size() < node.k_) {
        inf_frames.push_back(node.fid_);
        has_inf_distance = true;
      } else if (!has_inf_distance) {
        k_distance = compute_max_distance(node);
        if (k_distance > max_k_distance ||
            (k_distance == max_k_distance &&
             (evict_fid == -1 || static_cast<int>(get_oldest(fid)) < static_cast<int>(get_oldest(evict_fid))))) {
          max_k_distance = k_distance;
          evict_fid = node.fid_;
        }
      }
    }
  }

  if (has_inf_distance) {
    size_t oldest = std::numeric_limits<size_t>::max();
    size_t curr_oldest = UINT64_MAX;
    for (auto fid : inf_frames) {
      auto &node = node_store_.at(fid);
      curr_oldest = static_cast<size_t>(get_oldest(node.fid_));
      if (curr_oldest < oldest) {
        oldest = curr_oldest;
        evict_fid = node.fid_;
      }
    }
  }

  return evict_fid;
}

auto LRUKReplacer::Evict() -> std::optional<frame_id_t> {
  std::lock_guard<std::mutex> lock(latch_);
  if (curr_size_ == 0) {
    return std::nullopt;
  }

  frame_id_t evict_fid = ChooseEvictFrame();

  if (node_store_[evict_fid].is_evictable_) {
    --curr_size_;
  }
  node_store_.erase(evict_fid);
  return evict_fid;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  std::lock_guard<std::mutex> lock(latch_);
  auto &node = TryGetOrCreateNode(frame_id);

  node.history_.push_back(current_timestamp_);
  if (node.history_.size() > node.k_) {
    node.history_.pop_front();
  }

  ++current_timestamp_;
}

auto LRUKReplacer::TryGetOrCreateNode(frame_id_t frame_id) -> LRUKReplacer::LRUKNode & {
  auto [it, _] = node_store_.try_emplace(frame_id, frame_id, k_);
  return it->second;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);
  auto &node = TryGetOrCreateNode(frame_id);

  if (node.is_evictable_ == set_evictable) {
    return;
  }

  node.is_evictable_ = set_evictable;
  if (set_evictable) {
    ++curr_size_;
  } else {
    --curr_size_;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = node_store_.find(frame_id);
  if (it == node_store_.end()) {
    return;
  }

  auto &node = it->second;

  if (node.is_evictable_) {
    --curr_size_;
  }

  node_store_.erase(it);
}

auto LRUKReplacer::Size() -> size_t { return curr_size_; }

}  // namespace bustub
