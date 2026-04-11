//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.cpp
//
// Identification: src/storage/disk/disk_scheduler.cpp
//
// Copyright (c) 2015-2023, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/disk/disk_scheduler.h"
#include <utility>
#include "common/exception.h"
#include "storage/disk/disk_manager.h"

namespace bustub {

DiskScheduler::DiskScheduler(DiskManager *disk_manager) : disk_manager_(disk_manager) {
  // TODO(P1): remove this line after you have implemented the disk scheduler API

  // Spawn the background thread
  background_thread_.emplace([&]() -> void { StartWorkerThread(); });
}

DiskScheduler::~DiskScheduler() {
  // Put a `std::nullopt` in the queue to signal to exit the loop
  request_queue_.Put(std::nullopt);
  if (background_thread_.has_value()) {
    background_thread_->join();
  }
}

void DiskScheduler::Schedule(DiskRequest r) { request_queue_.Put(std::optional<bustub::DiskRequest>(std::move(r))); }

void DiskScheduler::StartWorkerThread() {
  while (true) {
    auto v = request_queue_.Get();

    if (!v.has_value()) {
      break;
    }
    DiskRequest &r = v.value();
    if (r.is_write_) {
      disk_manager_->WritePage(r.page_id_, std::as_const(r.data_));

    } else {
      disk_manager_->ReadPage(r.page_id_, r.data_);
    }
    r.callback_.set_value(true);
  }
}
}  // namespace bustub
