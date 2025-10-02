/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CryptoIOInterposerMac_h
#define mozilla_CryptoIOInterposerMac_h

#include <aio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

extern "C" {
ssize_t writev$NOCANCEL(int aFd, const struct iovec* aIov, int aIovCount);
ssize_t pwrite$NOCANCEL(int fildes, const void *buf, size_t nbyte, off_t offset);


typedef bool (*is_file_under_profile_t)(const char*);
typedef void (*register_cryptoio_interposers_t)(is_file_under_profile_t);

static is_file_under_profile_t is_file_under_profile = nullptr;
void register_cryptoio_interposers(is_file_under_profile_t);

typedef void (*unregister_cryptoio_interposers_t)();
void unregister_cryptoio_interposers();

}

namespace cryptoio {

} // namespace cryptoio

#endif // mozilla_CryptoIOInterposerMac_h
