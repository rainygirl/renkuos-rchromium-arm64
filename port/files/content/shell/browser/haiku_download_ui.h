// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_HAIKU_DOWNLOAD_UI_H_
#define CONTENT_SHELL_BROWSER_HAIKU_DOWNLOAD_UI_H_

#include <map>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"

namespace content {

// R Chromium's download progress: watches one DownloadManager and pushes
// every download's state into the shim's native Downloads window (see
// ui/ozone/platform/haiku/haiku_toolbar_bridge.h). The window's Cancel button
// comes back here. Owned by ShellDownloadManagerDelegate, UI thread only.
class HaikuDownloadUI : public DownloadManager::Observer,
                        public download::DownloadItem::Observer {
 public:
  explicit HaikuDownloadUI(DownloadManager* manager);
  HaikuDownloadUI(const HaikuDownloadUI&) = delete;
  HaikuDownloadUI& operator=(const HaikuDownloadUI&) = delete;
  ~HaikuDownloadUI() override;

  // DownloadManager::Observer:
  void OnDownloadCreated(DownloadManager* manager,
                         download::DownloadItem* item) override;
  void ManagerGoingDown(DownloadManager* manager) override;

  // download::DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override;
  void OnDownloadDestroyed(download::DownloadItem* item) override;

 private:
  struct Tracked {
    base::TimeTicks last_push;
    download::DownloadItem::DownloadState last_state;
  };

  void Push(download::DownloadItem* item);
  void Cancel(uint32_t id);
  void StopObservingAll();

  raw_ptr<DownloadManager> manager_;
  std::map<download::DownloadItem*, Tracked> items_;
  base::WeakPtrFactory<HaikuDownloadUI> weak_ptr_factory_{this};
};

}  // namespace content

#endif  // CONTENT_SHELL_BROWSER_HAIKU_DOWNLOAD_UI_H_
