// Copyright 2026 The RENKU project
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_LOOP_EPOLL_SHIM_HAIKU_H_
#define BASE_MESSAGE_LOOP_EPOLL_SHIM_HAIKU_H_

// Haiku has poll() but neither epoll nor kqueue, and Chromium removed its
// libevent pump, so message_pump_for_io.h maps every POSIX platform to
// MessagePumpEpoll.
//
// That pump already contains a complete poll() implementation --
// GetEventsPoll(), guarded by the kUsePollForMessagePumpEpoll feature, added
// for Android's scheduler behaviour rather than for portability. On that path
// epoll_event is used only as a plain struct to carry results, and the EPOLL*
// values only as bit flags mapped to and from the POLL* ones.
//
// So all Haiku needs is the type and the constants. The pump is compiled with
// its epoll_create/epoll_ctl/epoll_wait calls left out and the poll path
// forced on; see the changes to message_pump_epoll.{h,cc}.
//
// The bit values are the Linux ones. Nothing here is passed to a kernel: they
// are only compared against each other and against the kEpollToPollEvents
// table, so what matters is that they are distinct.

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>

enum {
  EPOLLIN = 0x001,
  EPOLLPRI = 0x002,
  EPOLLOUT = 0x004,
  EPOLLERR = 0x008,
  EPOLLHUP = 0x010,
  EPOLLRDHUP = 0x2000,
  EPOLLONESHOT = 1u << 30,
};

// Haiku has no POLLRDHUP. A peer closing its end of a socket shows up as
// POLLHUP there, which the pump already handles, so mapping EPOLLRDHUP onto
// nothing loses no events.
#if !defined(POLLRDHUP)
#define POLLRDHUP 0
#endif

typedef union epoll_data {
  void* ptr;
  int fd;
  uint32_t u32;
  uint64_t u64;
} epoll_data_t;

struct epoll_event {
  uint32_t events;
  epoll_data_t data;
};

// There is no eventfd() here: the pump wakes itself through a pair of Unix
// datagram sockets instead (see port-message-pump.py).

#endif  // BASE_MESSAGE_LOOP_EPOLL_SHIM_HAIKU_H_
