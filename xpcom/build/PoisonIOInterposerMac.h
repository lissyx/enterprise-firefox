/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <aio.h>

#ifndef mozilla_PoisonIOInterposerMac_h
#define mozilla_PoisonIOInterposerMac_h

extern "C" {
ssize_t writev$NOCANCEL(int aFd, const struct iovec* aIov, int aIovCount);
ssize_t pwrite$NOCANCEL(int fildes, const void *buf, size_t nbyte, off_t offset);
}

namespace poisonio {

struct InterposerCallbacks {
  void* (*start_write_timer)(int aFd, const void* aBuf, int aCount);
  void* (*start_writev_timer)(int aFd, const struct iovec* aIov, int aIovCount);
  void* (*start_pwrite_timer)(int aFd, const void* aBuf, size_t aNumBytes,
                              off_t aOffset);
  void* (*start_aio_write_timer)(struct aiocb* aAioCbp);
  void (*end_timer)(void *aTimer);
};

} // namespace poisonio

#endif // mozilla_PoisonIOInterposerMac_h
