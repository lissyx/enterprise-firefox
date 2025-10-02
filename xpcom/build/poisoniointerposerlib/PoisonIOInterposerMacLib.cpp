#include <aio.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <dlfcn.h>
#include <mutex>
#include <shared_mutex>
#include <sys/uio.h>

#include "PoisonIOInterposerMac.h"

static std::shared_mutex gCallbackMutex;

//
// Nop versions of the timer callbackis to use when no callbacks have been
// registered.
//

void* wrap_write_start(int aFd, const void* aBuf, int aCount) {
  return nullptr;
}

void* wrap_writev_start(int aFd, const struct iovec* aIov, int aIovCount) {
  return nullptr;
}

void* wrap_pwrite_start(int aFd, const void* aBuf, size_t aNumBytes,
                        off_t aOffset) {
  return nullptr;
}

void* wrap_aio_write_start(struct aiocb* aAioCbp) { return nullptr; }

void end_timer(void* aTimer) {}

static poisonio::InterposerCallbacks nopCallbacks = {
    wrap_write_start, wrap_writev_start, wrap_pwrite_start,
    wrap_aio_write_start, end_timer};

poisonio::InterposerCallbacks* timers = &nopCallbacks;

extern "C" {
void register_io_interposers(poisonio::InterposerCallbacks* aCallbacks);
void unregister_io_interposers();
}

void register_io_interposers(poisonio::InterposerCallbacks* aCallbacks) {
  std::unique_lock lock(gCallbackMutex);
  timers = aCallbacks;
}

void unregister_io_interposers() {
  std::unique_lock lock(gCallbackMutex);
  timers = &nopCallbacks;
}

ssize_t wrap_aio_write(struct aiocb* aAioCbp) {
  std::shared_lock lock(gCallbackMutex);
  void* timer = timers->start_aio_write_timer(aAioCbp);
  ssize_t rv = aio_write(aAioCbp);
  timers->end_timer(timer);
  return rv;
}

size_t wrap_write(int fildes, const void* buf, size_t nbyte) {
  std::shared_lock lock(gCallbackMutex);
  void* timer = timers->start_write_timer(fildes, buf, nbyte);
  size_t rv = write(fildes, buf, nbyte);
  timers->end_timer(timer);
  return rv;
}

ssize_t wrap_writev(int aFd, const struct iovec* aIov, int aIovCount) {
  std::shared_lock lock(gCallbackMutex);
  void* timer = timers->start_writev_timer(aFd, aIov, aIovCount);
  ssize_t rv = writev(aFd, aIov, aIovCount);
  timers->end_timer(timer);
  return rv;
}

ssize_t wrap_writev_NOCANCEL(int aFd, const struct iovec* aIov, int aIovCount) {
  std::shared_lock lock(gCallbackMutex);
  void* timer = timers->start_writev_timer(aFd, aIov, aIovCount);
  ssize_t rv = writev$NOCANCEL(aFd, aIov, aIovCount);
  timers->end_timer(timer);
  return rv;
}

ssize_t wrap_pwrite(int fildes, const void* buf, size_t nbyte, off_t offset) {
  std::shared_lock lock(gCallbackMutex);
  void* timer = timers->start_pwrite_timer(fildes, buf, nbyte, offset);
  ssize_t rv = pwrite(fildes, buf, nbyte, offset);
  timers->end_timer(timer);
  return rv;
}

ssize_t wrap_pwrite_NOCANCEL(int fildes, const void* buf, size_t nbyte,
                             off_t offset) {
  std::shared_lock lock(gCallbackMutex);
  void* timer = timers->start_pwrite_timer(fildes, buf, nbyte, offset);
  ssize_t rv = pwrite$NOCANCEL(fildes, buf, nbyte, offset);
  timers->end_timer(timer);
  return rv;
}

__attribute__((used)) static struct {
  const void* replacement;
  const void* replacee;
} interposers[] __attribute__((section("__DATA, __interpose"))) = {
    {(const void*)(unsigned long)&wrap_aio_write,
     (const void*)(unsigned long)&aio_write},
    {(const void*)(unsigned long)&wrap_write,
     (const void*)(unsigned long)&write},
    {(const void*)(unsigned long)&wrap_writev,
     (const void*)(unsigned long)&writev},
    {
        (const void*)(unsigned long)&wrap_writev_NOCANCEL,
        (const void*)(unsigned long)&writev$NOCANCEL,
    },
    {(const void*)(unsigned long)&wrap_pwrite,
     (const void*)(unsigned long)&pwrite},
    {(const void*)(unsigned long)&wrap_pwrite_NOCANCEL,
     (const void*)(unsigned long)&pwrite$NOCANCEL}};
