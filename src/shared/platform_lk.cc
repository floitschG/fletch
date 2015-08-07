// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#if defined(FLETCH_TARGET_OS_LK)

#include "src/shared/platform.h"

#include <platform.h>

#include <err.h>
#include <kernel/thread.h>
#include <kernel/semaphore.h>
#include <sys/types.h>

#include <stdlib.h>

void* operator new(unsigned int size) {
  return malloc(size);
}

void operator delete(void* data) {
  free(data);
}

void* operator new[](unsigned int size) {
  return malloc(size);
}

void operator delete[](void* data) {
  free(data);
}

extern "C" {
// void __aeabi_unwind_cpp_pr0() { }
// void __aeabi_unwind_cpp_pr1() { }

void abort() { printf("aborted\n"); while (true); }

///////////////// GUARDS ///////////////////
// (Guards are used to lock around static initializers)
// This implementation is NOT thread safe.

// A 32-bit, 4-byte-aligned static data value. The least significant 2 bits must
// be statically initialized to 0.
typedef unsigned guard_type;

// Test the lowest bit.
inline bool is_initialized(guard_type* guard_object) {
  return (*guard_object) & 1;
}

inline void set_initialized(guard_type* guard_object) {
  *guard_object |= 1;
}

int __cxa_guard_acquire(guard_type* guard_object) {
  return !is_initialized(guard_object);
}

void __cxa_guard_release(guard_type* guard_object) {
    *guard_object = 0;
  set_initialized(guard_object);
}

void __cxa_guard_abort(guard_type* guard_object) {
  *guard_object = 0;
}

int __gxx_personality_v0(int state,
                         int* unwind_exception,
                         int* context) {
  abort();
}

void __cxa_pure_virtual() {
  abort();
}

void __cxa_deleted_virtual() {
  abort();
}

void __cxa_end_cleanup() {
  abort();
}
}

namespace fletch {

void GetPathOfExecutable(char* path, size_t path_length) {
  // We are built into the kernel ...
  path[0] = '\0';
}

static uint64 time_launch;

void Platform::Setup() {
  time_launch = GetMicroseconds();

  // Make functions return EPIPE instead of getting SIGPIPE signal.
  // struct sigaction sa;
  // sa.sa_flags = 0;
  // sigemptyset(&sa.sa_mask);
  // sa.sa_handler = SIG_IGN;
  // sigaction(SIGPIPE, &sa, NULL);
}

uint64 Platform::GetMicroseconds() {
  lk_bigtime_t time = current_time_hires();
  return time;
}

uint64 Platform::GetProcessMicroseconds() {
  // Assume now is past time_launch.
  return GetMicroseconds() - time_launch;
}

int Platform::GetNumberOfHardwareThreads() {
  static int hardware_threads_cache_ = -1;
  if (hardware_threads_cache_ == -1) {
    // TODO(lk): Find a way to get number of hardware threads.
    hardware_threads_cache_ = 2;
  }
  return hardware_threads_cache_;
}

// Load file at 'uri'.
List<uint8> Platform::LoadFile(const char* name) {
  // Open the file.
  FILE* file = fopen(name, "rb");
  if (file == NULL) {
    printf("ERROR: Cannot open %s\n", name);
    return List<uint8>();
  }

  // Determine the size of the file.
  if (fseek(file, 0, SEEK_END) != 0) {
    printf("ERROR: Cannot seek in file %s\n", name);
    fclose(file);
    return List<uint8>();
  }
  int size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Read in the entire file.
  uint8* buffer = static_cast<uint8*>(malloc(size));
  int result = fread(buffer, 1, size, file);
  fclose(file);
  if (result != size) {
    printf("ERROR: Unable to read entire file %s\n", name);
    return List<uint8>();
  }
  return List<uint8>(buffer, size);
}

bool Platform::StoreFile(const char* uri, List<uint8> bytes) {
  // Open the file.
  FILE* file = fopen(uri, "wb");
  if (file == NULL) {
    printf("ERROR: Cannot open %s\n", uri);
    return false;
  }

  int result = fwrite(bytes.data(), 1, bytes.length(), file);
  fclose(file);
  if (result != bytes.length()) {
    printf("ERROR: Unable to write entire file %s\n", uri);
    return false;
  }

  return true;
}

const char* Platform::GetTimeZoneName(int64_t seconds_since_epoch) {
  // Unsupported. Return an empty string like V8 does.
  return "";
}

int Platform::GetTimeZoneOffset(int64_t seconds_since_epoch) {
  // Unsupported. Return zero like V8 does.
  return 0;
}

int Platform::GetLocalTimeZoneOffset() {
  // Unsupported.
  return 0;
}

// Constants used for mmap.
static const int kMmapFd = -1;
static const int kMmapFdOffset = 0;

VirtualMemory::VirtualMemory(int size) : size_(size) {
}

VirtualMemory::~VirtualMemory() {
}

bool VirtualMemory::IsReserved() const {
  return false;
}

bool VirtualMemory::Commit(uword address, int size, bool executable) {
  return NULL;
}

bool VirtualMemory::Uncommit(uword address, int size) {
  return NULL;
}

class LKMutex : public Mutex {
 public:
  LKMutex() { mutex_init(&mutex_);  }
  ~LKMutex() { mutex_destroy(&mutex_); }

  int Lock() { return mutex_acquire(&mutex_); }
  int Unlock() { return mutex_release(&mutex_); }

  // TODO(lk): Should this check whether we hold the lock or anyone does?
  bool IsLocked() {
    printf("islocked \n");
    if (is_mutex_held(&mutex_)) {
      printf("result true\n");
      return true;
    }
    printf("after is held \n");
    if (mutex_acquire_timeout(&mutex_, 0) == 0) {
      printf("after acquire timeout\n");
      mutex_release(&mutex_);
      printf("after release\n");
      return false;
    }
    return true;
  }

 private:
  mutex_t mutex_;   // lk kernel mutex.
};

Mutex* Platform::CreateMutex() {
  return new LKMutex();
}

class LKMonitor : public Monitor {
 public:
  LKMonitor() {
    mutex_init(&mutex_);
    mutex_init(&internal_);
    sem_init(&sem_, 1);
  }

  ~LKMonitor() {
    mutex_destroy(&mutex_);
    mutex_destroy(&internal_);
    sem_destroy(&sem_);
  }

  int Lock() { return mutex_acquire(&mutex_); }
  int Unlock() { return mutex_release(&mutex_); }

  int Wait() {
    mutex_acquire(&internal_);
    waiting_++;
    mutex_release(&internal_);
    mutex_release(&mutex_);
    sem_wait(&sem_);
    mutex_acquire(&mutex_);
    // TODO(lk): check errors.
    return 0;
  }

  bool Wait(uint64 microseconds) {
    uint64 us = Platform::GetMicroseconds() + microseconds;
    return WaitUntil(us);
  }

  bool WaitUntil(uint64 microseconds_since_epoch) {
    mutex_acquire(&internal_);
    waiting_++;
    mutex_release(&internal_);
    mutex_release(&mutex_);
    // TODO(lk): This is not really since epoch.
    status_t status = sem_timedwait(&sem_, microseconds_since_epoch);
    mutex_acquire(&mutex_);
    return status == ERR_TIMED_OUT;
  }

  int Notify() {
    mutex_acquire(&internal_);
    bool hasWaiting = waiting_ > 0;
    if (hasWaiting) --waiting_;
    mutex_release(&internal_);
    if (hasWaiting) {
      if (!sem_post(&sem_, false)) return -1;
    }
    return 0;
  }

  int NotifyAll() {
    mutex_acquire(&internal_);
    int towake = waiting_;
    waiting_ = 0;
    mutex_release(&internal_);
    while (towake-- > 0) {
     if (!sem_post(&sem_, false)) return -1;
    }
    return 0;
  }

 private:
  mutex_t mutex_;
  semaphore_t sem_;
  mutex_t internal_;
  int waiting_ = 0;
};

Monitor* Platform::CreateMonitor() {
  return new LKMonitor();
}

}  // namespace fletch

#endif  // defined(FLETCH_TARGET_OS_LK)
