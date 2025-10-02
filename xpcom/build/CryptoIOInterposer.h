/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CryptoIOInterposer_h
#define mozilla_CryptoIOInterposer_h

#include "mozilla/Types.h"
#include "nsIFile.h"
#include <stdio.h>

#if defined(XP_MACOSX) || defined(XP_WIN)

#  ifdef __cplusplus
namespace mozilla {

extern PathString profileRoot;
void CryptoIOInterposerSetProfilePath(PathString aProfilePath);
bool IsFileUnderProfile(const char* aFilename);

/**
 * Initialize IO poisoning, this is only safe to do on the main-thread when no
 * other threads are running.
 *
 * Please, note that this probably has performance implications as all
 */
void InitCryptoIOInterposer();

/**
 * Clear IO poisoning, this is only safe to do on the main-thread when no other
 * threads are running.
 * Never called! See bug 1647107.
 */
void ClearCryptoIOInterposer();

}  // namespace mozilla
#  endif /* __cplusplus */

#else /* defined(XP_MACOSX) || defined(XP_WIN) */

#  ifdef __cplusplus
namespace mozilla {
inline void InitCryptoIOInterposer() {}
inline void ClearCryptoIOInterposer() {}
#    ifdef XP_MACOSX
inline void OnlyReportDirtyWrites() {}
#    endif /* XP_MACOSX */
}  // namespace mozilla
#  endif   /* __cplusplus */

#endif /* XP_WIN || XP_MACOSX */

#endif  // mozilla_CryptoIOInterposer_h
