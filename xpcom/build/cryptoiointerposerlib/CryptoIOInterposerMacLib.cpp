#include <aio.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <dlfcn.h>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <sys/uio.h>

#include "CryptoIOInterposerMac.h"
#include "CryptoIOInterposerCommon.h"

static bool apply_encryption = false;
static bool cryptoio_debug = false;
static FILE* cryptoio_debug_file = stderr;

#define DBG(args...) { if (cryptoio_debug) fprintf(cryptoio_debug_file, args); }
#define ERR(args...) { fprintf(stderr, args); }

void register_cryptoio_interposers(is_file_under_profile_t callback) {
  is_file_under_profile = callback;
  fprintf(stderr, "[%s:%d] received found is_file_under_profile=%p\n", __PRETTY_FUNCTION__, getpid(), is_file_under_profile);

  apply_encryption = getenv("MOZ_ENABLE_PROFILE_ENCRYPTION") != nullptr;
  const char* cryptoio_debug_value = getenv("MOZ_PROFILE_ENCRYPTION_DEBUG");
  cryptoio_debug = cryptoio_debug_value != nullptr;
  if (cryptoio_debug && strcmp(cryptoio_debug_value, "1") != 0) {
    cryptoio_debug_file = fopen(cryptoio_debug_value, "w");
    if (!cryptoio_debug_file) {
      perror("fopen(cryptoio_debug_file)");
    }
  }
}

void unregister_cryptoio_interposers() {
  if (cryptoio_debug_file != stderr) {
    fclose(cryptoio_debug_file);
  }
  is_file_under_profile = nullptr;
}

static inline bool maybe_is_file_under_profile(const char* aPath) {
  if (aPath && is_file_under_profile) {
    return is_file_under_profile(aPath);
  }
  return false;
}

const void* do_encrypt(const char* op, size_t bytes, const void* buffer, bool* alloc) {
  if (apply_encryption) {
    char* new_buffer = (char*)malloc(sizeof(char)*bytes);
    DBG("[%d] encrypt %s(%zu, %p)\n", getpid(), op, bytes, buffer);
    for (size_t i = 0; i < bytes; ++i) {
      char n_c = ((char*)(buffer))[i] - 13;
      new_buffer[i] = n_c;
    }
    *alloc = true;
    return (const void*)new_buffer;
  }
  return buffer;
}

void do_decrypt(const char* op, size_t bytes, void* buffer) {
  if (apply_encryption) {
    DBG("[%d] encrypt %s(%zu, %p)\n", getpid(), op, bytes, buffer);
    for (size_t i = 0; i < bytes; ++i) {
      char n_c = ((char*)(buffer))[i] + 13;
      ((char*)buffer)[i] = n_c;
    }
  }
}

std::unordered_map<int, const char*> FDsOfInterest{};
std::unordered_map<FILE*, const char*> FilePtrsOfInterest{};

bool isValidFD(int aFD) {
  return aFD > 2;
}

bool isValidFP(FILE* aFile) {
  return aFile && aFile != stdin && aFile != stdout && aFile != stderr;
}

bool isFDTracked(int aFD) {
  return isValidFD(aFD) && FDsOfInterest.find(aFD) != FDsOfInterest.end();
}

bool isFilePtrTracked(FILE* aFile) {
  return isValidFP(aFile) && FilePtrsOfInterest.find(aFile) != FilePtrsOfInterest.end();
}

bool isValid(int aFD, FILE* aFile, const char* aFilename) {
  if (!isValidFP(aFile) && !isValidFD(aFD)) {
    return false;
  }

  if (aFilename && !maybe_is_file_under_profile(aFilename)) {
    return false;
  }

  return true;
}

void trackFDOrFilePtrIfUnderProfile(int aFD, FILE* aFile, const char* aFilename) {
  if (!isValid(aFD, aFile, aFilename)) {
    return;
  }

  if (aFile) {
    if (isFilePtrTracked(aFile)) {
      const char* existingFilename = strdup(FilePtrsOfInterest[aFile]);
      DBG("DUPLICATED HANDLE: %p for '%s' (existing) and '%s' (new)\n", aFile, existingFilename, aFilename);
      // MOZ_CRASH("Existing file handle");
      return;
    }

    DBG("[%d] tracking %p (%s)\n", getpid(), aFile, aFilename);
    FilePtrsOfInterest[aFile] = strdup(aFilename);
    return;
  }

  if (aFD > 2) {
    if (isFDTracked(aFD)) {
      const char* existingFilename = strdup(FDsOfInterest[aFD]);
      DBG( "DUPLICATED HANDLE: %d for '%s' (existing) and '%s' (new)\n", aFD, existingFilename, aFilename);
      // MOZ_CRASH("Existing file handle");
      return;
    }

    DBG( "[%d] tracking %d (%s)\n", getpid(), aFD, aFilename);
    FDsOfInterest[aFD] = strdup(aFilename);
    return;
  }
}

void untrackFDOrFilePtrIfUnderProfile(int aFD, FILE* aFile) {
  if (!isValid(aFD, aFile, nullptr)) {
    return;
  }

  if (aFile && isFilePtrTracked(aFile)) {
    FilePtrsOfInterest.erase(aFile);
    DBG( "[%d] untracking %p\n", getpid(), aFile);
  }

  if (aFD > 2 && isFDTracked(aFD)) {
    FDsOfInterest.erase(aFD);
    DBG( "[%d] untracking %d\n", getpid(), aFD);
  }

  // MOZ_ASSERT(removed == 1, "Removed one entry"); 
}

int wrap_open(const char* pathname, int flags, ...) {
  va_list args;
  va_start (args, flags);
  mode_t mode = va_arg (args, int);
  va_end (args);
  int fd = open(pathname, flags, mode);
  if (cryptoio_debug && fd < 0) {
    perror("libencrypt: open()");
  }

  trackFDOrFilePtrIfUnderProfile(fd, nullptr, pathname);
  return fd;
}

int wrap_creat(const char* pathname, mode_t mode) {
  int fd = creat(pathname, mode);
  trackFDOrFilePtrIfUnderProfile(fd, nullptr, pathname);
  return fd;
}

int wrap_openat(int dirfd, const char* pathname, int flags, ...) {
  va_list args;
  va_start (args, flags);
  mode_t mode = va_arg (args, int);
  va_end (args);
  int fd = openat(dirfd, pathname, flags, mode);
  trackFDOrFilePtrIfUnderProfile(fd, nullptr, pathname);
  return fd;
}

int wrap_close(int fd) {
  untrackFDOrFilePtrIfUnderProfile(fd, nullptr);
  return close(fd);
}

ssize_t wrap_read(int fd, void* buf, size_t count) {
    ssize_t retval = 0;
    retval = read(fd, buf, count);
    if (isFDTracked(fd)) {
      do_decrypt("read", retval, buf);
    }
    return retval;
}

ssize_t wrap_pread(int fd, void* buf, size_t count, off_t offset) {
    ssize_t retval = 0;
    retval = pread(fd, buf, count, offset);
    if (isFDTracked(fd)) {
      do_decrypt("pread", retval, buf);
    }
    return retval;
}

ssize_t wrap_write(int fd, const void* buf, size_t count) {
    ssize_t retval = 0;
    void* rbuf = (void*)buf;
    bool free_needed = false;
    if (isFDTracked(fd)) {
      rbuf = (void*)do_encrypt("write", count, buf, &free_needed);
    }
    retval = write(fd, rbuf, count);
    if (free_needed) {
      free(rbuf);
    }
    return retval;
}

ssize_t wrap_pwrite(int fd, const void* buf, size_t count, off_t offset) {
    ssize_t retval = 0;
    void* rbuf = (void*)buf;
    bool free_needed = false;
    if (isFDTracked(fd)) {
      rbuf = (void*)do_encrypt("pwrite", count, buf, &free_needed);
    }
    retval = pwrite(fd, rbuf, count, offset);
    if (free_needed) {
      free(rbuf);
    }
    return retval;
}

FILE* wrap_fopen(const char* pathname, const char* mode) {
  FILE* fp = fopen(pathname, mode);
  if (cryptoio_debug && !fp) {
    perror("libencrypt: fopen()");
  }
  trackFDOrFilePtrIfUnderProfile(0, fp, pathname);
  return fp;
}

int wrap_fclose(FILE* fp) {
  untrackFDOrFilePtrIfUnderProfile(0, fp);
  return fclose(fp);
}

size_t wrap_fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    size_t retval = 0;
    retval = fread(ptr, size, nmemb, stream);
    if (isFilePtrTracked(stream)) {
      do_decrypt("fread", size * nmemb, ptr);
    }
    return retval;
}

size_t wrap_fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    size_t retval = 0;
    void* rptr = (void*)ptr;
    bool free_needed = false;
    if (isFilePtrTracked(stream)) {
      rptr = (void*)do_encrypt("fwrite", size * nmemb, ptr, &free_needed);
    }
    retval = fwrite(rptr, size, nmemb, stream);
    if (free_needed) {
      free(rptr);
    }
    return retval;
}

ssize_t wrap_aio_write(struct aiocb* aAioCbp) {
  ssize_t rv = aio_write(aAioCbp);
  return rv;
}

ssize_t wrap_writev(int aFd, const struct iovec* aIov, int aIovCount) {
  ssize_t rv = writev(aFd, aIov, aIovCount);
  return rv;
}

/* ssize_t wrap_writev$NOCANCEL(int aFd, const struct iovec* aIov, int aIovCount) {
  ssize_t rv = writev$NOCANCEL(aFd, aIov, aIovCount);
  return rv;
} */

/* ssize_t wrap_pwrite$NOCANCEL(int fildes, const void* buf, size_t nbyte,
                             off_t offset) {
  ssize_t rv = pwrite$NOCANCEL(fildes, buf, nbyte, offset);
  return rv;
} */

#define WRAP_SYMBOL(symbol) { (const void*)(unsigned long)&wrap_##symbol, (const void*)(unsigned long)&symbol }

__attribute__((used)) static struct {
  const void* replacement;
  const void* replacee;
} interposers[] __attribute__((section("__DATA, __interpose"))) = {
    WRAP_SYMBOL(aio_write),
    WRAP_SYMBOL(open),
    WRAP_SYMBOL(fopen),
    WRAP_SYMBOL(creat),
    WRAP_SYMBOL(openat),
    WRAP_SYMBOL(close),
    WRAP_SYMBOL(fclose),
    WRAP_SYMBOL(read),
    WRAP_SYMBOL(fread),
    WRAP_SYMBOL(pread),
    WRAP_SYMBOL(write),
    WRAP_SYMBOL(fwrite),
    WRAP_SYMBOL(pwrite),
    WRAP_SYMBOL(writev),
    // WRAP_SYMBOL(writev$NOCANCEL),
    WRAP_SYMBOL(pwrite)
    // WRAP_SYMBOL(pwritev$NOCANCEL),
};
