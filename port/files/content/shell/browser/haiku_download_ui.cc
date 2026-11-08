// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/haiku_download_ui.h"

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "ui/ozone/platform/haiku/haiku_toolbar_bridge.h"

namespace content {

namespace {

// Progress is pushed at most this often; state changes always go through.
constexpr base::TimeDelta kPushInterval = base::Milliseconds(250);

ui::HaikuDownloadState StateOf(const download::DownloadItem* item) {
  switch (item->GetState()) {
    case download::DownloadItem::COMPLETE:
      return ui::HaikuDownloadState::kComplete;
    case download::DownloadItem::CANCELLED:
      return ui::HaikuDownloadState::kCancelled;
    case download::DownloadItem::INTERRUPTED:
      return ui::HaikuDownloadState::kFailed;
    default:
      return ui::HaikuDownloadState::kInProgress;
  }
}

}  // namespace

HaikuDownloadUI::HaikuDownloadUI(DownloadManager* manager)
    : manager_(manager) {
  manager_->AddObserver(this);
  ui::HaikuDownloadsSetCancelCallback(base::BindRepeating(
      &HaikuDownloadUI::Cancel, weak_ptr_factory_.GetWeakPtr()));
}

HaikuDownloadUI::~HaikuDownloadUI() {
  StopObservingAll();
}

void HaikuDownloadUI::StopObservingAll() {
  for (auto& entry : items_) {
    entry.first->RemoveObserver(this);
  }
  items_.clear();
  if (manager_) {
    manager_->RemoveObserver(this);
    manager_ = nullptr;
  }
}

void HaikuDownloadUI::OnDownloadCreated(DownloadManager* manager,
                                        download::DownloadItem* item) {
  // Downloads restored from history are finished business; transient ones
  // (Save Page's parts, and the like) are not the user's.
  if (item->IsTransient() ||
      item->GetState() != download::DownloadItem::IN_PROGRESS) {
    return;
  }
  items_[item] = Tracked{base::TimeTicks(), item->GetState()};
  item->AddObserver(this);
  Push(item);
}

void HaikuDownloadUI::ManagerGoingDown(DownloadManager* manager) {
  StopObservingAll();
}

void HaikuDownloadUI::OnDownloadUpdated(download::DownloadItem* item) {
  Push(item);
}

void HaikuDownloadUI::OnDownloadDestroyed(download::DownloadItem* item) {
  item->RemoveObserver(this);
  items_.erase(item);
}

void HaikuDownloadUI::Push(download::DownloadItem* item) {
  auto it = items_.find(item);
  if (it == items_.end()) {
    return;
  }
  // Nothing to show until the delegate has picked a file name.
  const base::FilePath path = item->GetTargetFilePath();
  if (path.empty()) {
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  const bool state_changed = it->second.last_state != item->GetState();
  if (!state_changed && !it->second.last_push.is_null() &&
      now - it->second.last_push < kPushInterval) {
    return;
  }
  it->second.last_push = now;
  it->second.last_state = item->GetState();

  const bool in_progress =
      item->GetState() == download::DownloadItem::IN_PROGRESS;
  ui::HaikuDownloadsUpdate(
      item->GetId(), path.BaseName().value(), path.value(),
      item->GetReceivedBytes(), item->GetTotalBytes() > 0
                                    ? item->GetTotalBytes()
                                    : -1,
      in_progress && !item->IsPaused() ? item->CurrentSpeed() : 0,
      StateOf(item));

  if (!in_progress) {
    // Finished one way or the other; the window keeps the last state.
    item->RemoveObserver(this);
    items_.erase(it);
  }
}

void HaikuDownloadUI::Cancel(uint32_t id) {
  if (!manager_) {
    return;
  }
  if (download::DownloadItem* item = manager_->GetDownload(id)) {
    item->Cancel(/*user_cancel=*/true);
  }
}

}  // namespace content
