/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CryptoIOInterposer.h"
#include "CryptoIOInterposerMac.h"

#include "mozilla/Assertions.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/DarwinFileUtils.h"
#include "mozilla/IOInterposer.h"
#include "mozilla/Mutex.h"
#include "mozilla/SmallArrayLRUCache.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "nsTArray.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/ProcessedStack.h"
#include "mozilla/UniquePtrExtensions.h"
#include "nsPrintfCString.h"
#include "mozilla/StackWalk.h"
#include "nsTraceRefcnt.h"
#include "prio.h"

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <aio.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

// Keep track of cryptoed state. Notice that there is no reason to lock access
// to this variable as it's only changed in InitCryptoIOInterposer and
// ClearCryptoIOInterposer which may only be called on the main-thread when no
// other threads are running.
static bool sIOCryptoed = false;

/************************ Internal NT API Declarations ************************/


/*************************** Auxiliary Declarations ***************************/

}  // namespace

/******************************** IO Cryptoing ********************************/

namespace mozilla {

void InitCryptoIOInterposer() {
  // Don't crypto twice... as this function may only be invoked on the main
  // thread when no other threads are running, it safe to allow multiple calls
  // to InitCryptoIOInterposer() without complaining (ie. failing assertions).
  if (sIOCryptoed) {
    return;
  }
  sIOCryptoed = true;

  register_cryptoio_interposers_t register_cryptoio_interposers =
      (register_cryptoio_interposers_t)dlsym(RTLD_DEFAULT, "register_cryptoio_interposers");
  if (register_cryptoio_interposers) {
    register_cryptoio_interposers(IsFileUnderProfile);
  }

}

void ClearCryptoIOInterposer() {
  MOZ_ASSERT(false, "Never called! See bug 1647107");
  if (sIOCryptoed) {
    // Destroy the DLL interceptor
    sIOCryptoed = false;

    unregister_cryptoio_interposers_t unregister_cryptoio_interposers =
        (unregister_cryptoio_interposers_t)dlsym(RTLD_DEFAULT, "unregister_cryptoio_interposers");
    if (unregister_cryptoio_interposers) {
      unregister_cryptoio_interposers();
    }
  }
}

}  // namespace mozilla
