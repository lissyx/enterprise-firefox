/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PoisonIOInterposer.h"
#include "PoisonIOInterposerMac.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/IOInterposer.h"
#include "mozilla/Mutex.h"
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

#ifdef MOZ_REPLACE_MALLOC
#  include "replace_malloc_bridge.h"
#endif

namespace {

// Bit tracking if poisoned writes are enabled
static bool sIsEnabled = false;

// Check if writes are dirty before reporting IO
static bool sOnlyReportDirtyWrites = false;

// Routines for write validation
bool IsValidWrite(int aFd, const void* aWbuf, size_t aCount);
bool IsIPCWrite(int aFd, const struct stat& aBuf);

// Callbacks to pass to the interposer library.
poisonio::InterposerCallbacks interposerCallbacks;

/******************************** IO AutoTimer ********************************/

/**
 * RAII class for timing the duration of an I/O call and reporting the result
 * to the mozilla::IOInterposeObserver API.
 */
class MacIOAutoObservation : public mozilla::IOInterposeObserver::Observation {
 public:
  MacIOAutoObservation(mozilla::IOInterposeObserver::Operation aOp, int aFd)
      : mozilla::IOInterposeObserver::Observation(
            aOp, sReference, sIsEnabled && !mozilla::IsDebugFile(aFd)),
        mFd(aFd),
        mHasQueriedFilename(false) {}

  MacIOAutoObservation(mozilla::IOInterposeObserver::Operation aOp, int aFd,
                       const void* aBuf, size_t aCount)
      : mozilla::IOInterposeObserver::Observation(
            aOp, sReference,
            sIsEnabled && !mozilla::IsDebugFile(aFd) &&
                IsValidWrite(aFd, aBuf, aCount)),
        mFd(aFd),
        mHasQueriedFilename(false) {}

  // Custom implementation of
  // mozilla::IOInterposeObserver::Observation::Filename
  void Filename(nsAString& aFilename) override;

  ~MacIOAutoObservation() { Report(); }

 private:
  int mFd;
  bool mHasQueriedFilename;
  nsString mFilename;
  static const char* sReference;
};

const char* MacIOAutoObservation::sReference = "PoisonIOInterposer";

// Get filename for this observation
void MacIOAutoObservation::Filename(nsAString& aFilename) {
  // If mHasQueriedFilename is true, then we already have it
  if (mHasQueriedFilename) {
    aFilename = mFilename;
    return;
  }

  char filename[MAXPATHLEN];
  if (fcntl(mFd, F_GETPATH, filename) != -1) {
    CopyUTF8toUTF16(filename, mFilename);
  } else {
    mFilename.Truncate();
  }
  mHasQueriedFilename = true;

  aFilename = mFilename;
}

/****************************** Write Validation ******************************/

// We want to detect "actual" writes, not IPC. Some IPC mechanisms are
// implemented with file descriptors, so filter them out.
bool IsIPCWrite(int aFd, const struct stat& aBuf) {
  if ((aBuf.st_mode & S_IFMT) == S_IFIFO) {
    return true;
  }

  if ((aBuf.st_mode & S_IFMT) != S_IFSOCK) {
    return false;
  }

  sockaddr_storage address;
  socklen_t len = sizeof(address);
  if (getsockname(aFd, (sockaddr*)&address, &len) != 0) {
    return true;  // Ignore the aFd if we can't find what it is.
  }

  return address.ss_family == AF_UNIX;
}

// We want to report actual disk IO not things that don't move bits on the disk
bool IsValidWrite(int aFd, const void* aWbuf, size_t aCount) {
  // Ignore writes of zero bytes, Firefox does some during shutdown.
  if (aCount == 0) {
    return false;
  }

  {
    struct stat buf;
    int rv = fstat(aFd, &buf);
    if (rv != 0) {
      return true;
    }

    if (IsIPCWrite(aFd, buf)) {
      return false;
    }
  }

  // For writev we pass a nullptr aWbuf. We should only get here from
  // dbm, and it uses write, so assert that we have aWbuf.
  if (!aWbuf) {
    return true;
  }

  // Break, here if we're allowed to report non-dirty writes
  if (!sOnlyReportDirtyWrites) {
    return true;
  }

  // As a really bad hack, accept writes that don't change the on disk
  // content. This is needed because dbm doesn't keep track of dirty bits
  // and can end up writing the same data to disk twice. Once when the
  // user (nss) asks it to sync and once when closing the database.
  auto wbuf2 = mozilla::MakeUniqueFallible<char[]>(aCount);
  if (!wbuf2) {
    return true;
  }
  off_t pos = lseek(aFd, 0, SEEK_CUR);
  if (pos == -1) {
    return true;
  }
  ssize_t r = read(aFd, wbuf2.get(), aCount);
  if (r < 0 || (size_t)r != aCount) {
    return true;
  }
  int cmp = memcmp(aWbuf, wbuf2.get(), aCount);
  if (cmp != 0) {
    return true;
  }
  off_t pos2 = lseek(aFd, pos, SEEK_SET);
  if (pos2 != pos) {
    return true;
  }

  // Otherwise this is not a valid write
  return false;
}

/**************************** Interposer Callbacks ****************************/

using mozilla::UniquePtr;

/*
 * Timer callbacks invoked from the interposing library to time IO operations.
 */

void* start_write_timer(int aFd, const void* aBuf, int aCount) {
  return new MacIOAutoObservation(mozilla::IOInterposeObserver::OpWrite, aFd,
                                  aBuf, aCount);
}

void* start_writev_timer(int aFd, const struct iovec* aIov, int aIovCount) {
  return new MacIOAutoObservation(mozilla::IOInterposeObserver::OpWrite, aFd,
                                  nullptr, aIovCount);
}

void* start_pwrite_timer(int aFd, const void* aBuf, size_t aNumBytes,
                         off_t aOffset) {
  return new MacIOAutoObservation(mozilla::IOInterposeObserver::OpWrite, aFd);
}

void* start_aio_write_timer(struct aiocb* aAioCbp) {
  return new MacIOAutoObservation(mozilla::IOInterposeObserver::OpWrite,
                                  aAioCbp->aio_fildes);
}

void end_timer(void* aTimer) {
  UniquePtr<MacIOAutoObservation> timer(
      static_cast<MacIOAutoObservation*>(aTimer));
}

}  // namespace

/****************************** IO Poisoning **********************************/

namespace mozilla {

void InitPoisonIOInterposer() {
  interposerCallbacks.start_write_timer = start_write_timer;
  interposerCallbacks.start_writev_timer = start_writev_timer;
  interposerCallbacks.start_pwrite_timer = start_pwrite_timer;
  interposerCallbacks.start_aio_write_timer = start_aio_write_timer;
  interposerCallbacks.end_timer = end_timer;

  // Enable reporting from poisoned write methods
  sIsEnabled = true;

  // Make sure we only poison writes once!
  static bool WritesArePoisoned = false;
  if (WritesArePoisoned) {
    return;
  }
  WritesArePoisoned = true;

  // stdout and stderr are OK.
  MozillaRegisterDebugFD(1);
  MozillaRegisterDebugFD(2);

#ifdef MOZ_REPLACE_MALLOC
  // The contract with InitDebugFd is that the given registry can be used
  // at any moment, so the instance needs to persist longer than the scope
  // of this functions.
  static DebugFdRegistry registry;
  ReplaceMalloc::InitDebugFd(registry);
#endif

  typedef void (*register_io_interposers_t)(poisonio::InterposerCallbacks*);
  void (*register_io_interposers)(poisonio::InterposerCallbacks*);

  register_io_interposers =
      (register_io_interposers_t)dlsym(RTLD_DEFAULT, "register_io_interposers");
  if (register_io_interposers) {
    register_io_interposers(&interposerCallbacks);
  }
}

void OnlyReportDirtyWrites() { sOnlyReportDirtyWrites = true; }

// Never called! See bug 1647107.
void ClearPoisonIOInterposer() {
  // Not sure how or if we can unpoison the functions. Would be nice, but no
  // worries we won't need to do this anyway.
  sIsEnabled = false;
}

}  // namespace mozilla
