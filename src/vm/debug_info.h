// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#ifndef SRC_VM_DEBUG_INFO_H_
#define SRC_VM_DEBUG_INFO_H_

#include "src/vm/object.h"

namespace fletch {

class DebugInfo {
 public:
  static const int kNoBreakpointId = -1;

  virtual ~DebugInfo() { }

  virtual bool ShouldBreak(uint8_t* bcp, Object** sp) = 0;
  virtual int SetBreakpoint(Function* function,
                            int bytecode_index,
                            bool one_shot = false,
                            Coroutine* coroutine = NULL,
                            int stack_height = 0) = 0;
  virtual bool DeleteBreakpoint(int id) = 0;
  virtual bool is_stepping() const = 0;
  virtual void set_is_stepping(bool value) = 0;
  virtual bool is_at_breakpoint() const = 0;
  virtual int current_breakpoint_id() const = 0;

  virtual void set_current_breakpoint(int id) = 0;

  virtual void clear_current_breakpoint() = 0;

  // GC support for process GCs.
  virtual void VisitPointers(PointerVisitor* visitor) = 0;

  // GC support for program GCs.
  virtual void VisitProgramPointers(PointerVisitor* visitor) = 0;
  virtual void UpdateBreakpoints() = 0;
};

}  // namespace fletch

#endif  // SRC_VM_DEBUG_INFO_H_
