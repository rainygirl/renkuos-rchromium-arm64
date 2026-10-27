// Copyright 2026 The RENKU project
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chrome.system.cpu's per-processor times. Haiku counts only how long each
// CPU has been busy (cpu_info::active_time, in microseconds since boot); it
// does not split that into user and kernel time, so it is all reported as
// user time, and idle time is the rest of the uptime.

#include "extensions/browser/api/system_cpu/cpu_info_provider.h"

#include <OS.h>

#include <vector>

namespace extensions {

bool CpuInfoProvider::QueryCpuTimePerProcessor(
    std::vector<api::system_cpu::ProcessorInfo>* infos) {
  DCHECK(infos);

  ::system_info system;
  if (get_system_info(&system) != B_OK || system.cpu_count == 0) {
    return false;
  }
  std::vector<::cpu_info> cpus(system.cpu_count);
  if (get_cpu_info(0, system.cpu_count, cpus.data()) != B_OK) {
    return false;
  }

  const bigtime_t uptime = system_time();
  for (uint32 i = 0; i < system.cpu_count && i < infos->size(); i++) {
    const bigtime_t active = cpus[i].active_time;
    const bigtime_t idle = uptime > active ? uptime - active : 0;
    infos->at(i).usage.kernel = 0;
    infos->at(i).usage.user = static_cast<double>(active);
    infos->at(i).usage.idle = static_cast<double>(idle);
    infos->at(i).usage.total = static_cast<double>(active + idle);
  }
  return true;
}

}  // namespace extensions
