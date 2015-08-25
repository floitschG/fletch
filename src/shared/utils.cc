// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#include <stdarg.h>
#include <stdlib.h>

#include "src/shared/utils.h"

#include "src/shared/platform.h"

namespace fletch {

#ifdef FLETCH_ENABLE_PRINT_INTERCEPTORS
Mutex* Print::mutex_ = Platform::CreateMutex();
PrintInterceptor* Print::interceptor_ = NULL;
Atomic<bool> Print::standard_output_enabled_ = true;
#endif

void Print::Out(const char* format, ...) {
#ifdef FLETCH_ENABLE_PRINT_INTERCEPTORS
  va_list args;
  va_start(args, format);
  int size = vsnprintf(NULL, 0, format, args);
  va_end(args);
  char* message = reinterpret_cast<char*>(malloc(size + 1));
  va_start(args, format);
  int printed = vsnprintf(message, size + 1, format, args);
  ASSERT(printed == size);
  va_end(args);
  if (standard_output_enabled_) {
    fputs(message, stdout);
    fflush(stdout);
  }
  ScopedLock scope(mutex_);
  for (PrintInterceptor* interceptor = interceptor_;
       interceptor != NULL;
       interceptor = interceptor->next_) {
    interceptor_->Out(message);
  }
  free(message);
#else
  va_list args;
  va_start(args, format);
  vfprintf(stdout, format, args);
  va_end(args);
#endif  // FLETCH_ENABLE_PRINT_INTERCEPTORS
}

void Print::Error(const char* format, ...) {
#ifdef FLETCH_ENABLE_PRINT_INTERCEPTORS
  va_list args;
  va_start(args, format);
  int size = vsnprintf(NULL, 0, format, args);
  va_end(args);
  char* message = reinterpret_cast<char*>(malloc(size + 1));
  va_start(args, format);
  int printed = vsnprintf(message, size + 1, format, args);
  ASSERT(printed == size);
  va_end(args);
  if (standard_output_enabled_) {
    fputs(message, stderr);
    fflush(stderr);
  }
  ScopedLock scope(mutex_);
  for (PrintInterceptor* interceptor = interceptor_;
       interceptor != NULL;
       interceptor = interceptor->next_) {
     interceptor_->Error(message);
  }
  free(message);
#else
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
#endif  // FLETCH_ENABLE_PRINT_INTERCEPTORS
}

void Print::RegisterPrintInterceptor(PrintInterceptor* interceptor) {
#ifdef FLETCH_ENABLE_PRINT_INTERCEPTORS
  ScopedLock scope(mutex_);
  ASSERT(!interceptor->next_);
  interceptor->next_ = interceptor_;
  interceptor_ = interceptor;
#else
  UNIMPLEMENTED();
#endif  // FLETCH_ENABLE_PRINT_INTERCEPTORS
}

void Print::UnregisterPrintInterceptors() {
#ifdef FLETCH_ENABLE_PRINT_INTERCEPTORS
  ScopedLock scope(mutex_);
  delete interceptor_;
  interceptor_ = NULL;
#endif  // FLETCH_ENABLE_PRINT_INTERCEPTORS
}

uint32 Utils::StringHash(const uint16* data, int length) {
  // This implementation is based on the public domain MurmurHash
  // version 2.0. The constants M and R have been determined work
  // well experimentally.
  const uint32 M = 0x5bd1e995;
  const int R = 24;
  int size = length * sizeof(uint16);
  uint32 hash = size;

  // We'll be reading four bytes at a time. On certain systems that
  // is only allowed if the pointers are properly aligned.
  ASSERT(IsAligned(reinterpret_cast<uword>(data), 4));

  // Mix four bytes at a time into the hash.
  const uint8* cursor = reinterpret_cast<const uint8*>(data);
  while (size >= 4) {
    uint32 part = *reinterpret_cast<const uint32*>(cursor);
    part *= M;
    part ^= part >> R;
    part *= M;
    hash *= M;
    hash ^= part;
    cursor += 4;
    size -= 4;
  }

  // Handle the last two bytes of the string if necessary.
  if (size != 0) {
    ASSERT(size == 2);
    hash ^= *reinterpret_cast<const uint16*>(cursor);
    hash *= M;
  }

  // Do a few final mixes of the hash to ensure the last few bytes are
  // well-incorporated.
  hash ^= hash >> 13;
  hash *= M;
  hash ^= hash >> 15;
  return hash;
}

}  // namespace fletch
