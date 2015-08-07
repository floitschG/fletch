// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#if defined(FLETCH_TARGET_OS_LK)

#include "src/vm/thread.h"

#include <errno.h>
#include <stdio.h>

#include "src/shared/platform.h"

namespace fletch {

bool Thread::IsCurrent(const ThreadIdentifier* thread) {
  return thread->IsSelf();
}

void Thread::Run(RunSignature run, void* data) {
  // TODO(lk): lk threads have int return values.
  thread_t *thread = thread_create("Dart thread",
      (thread_start_routine) run, data,
      DEFAULT_PRIORITY, 8192 /* DEFAULT_STACK_SIZE */);
  int result = thread_resume(thread);
  if (result != 0) {
    fprintf(stderr, "Error %d", result);
  }
}

}  // namespace fletch

#endif  // defined(FLETCH_TARGET_OS_LK)
