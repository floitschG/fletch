// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#if defined(FLETCH_TARGET_ARM)

#include "src/shared/bytecodes.h"
#include "src/shared/names.h"
#include "src/shared/selectors.h"

#include "src/vm/assembler.h"
#include "src/vm/generator.h"
#include "src/vm/interpreter.h"
#include "src/vm/intrinsics.h"
#include "src/vm/object.h"
#include "src/vm/process.h"
#include "src/vm/program.h"

#define __ assembler()->

namespace fletch {

class InterpreterGenerator {
 public:
  explicit InterpreterGenerator(Assembler* assembler)
      : assembler_(assembler) { }

  void Generate();

  virtual void GeneratePrologue() = 0;
  virtual void GenerateEpilogue() = 0;

#define V(name, branching, format, size, stack_diff, print)      \
  virtual void Do##name() = 0;
  BYTECODES_DO(V)
#undef V

#define V(name) \
  virtual void DoIntrinsic##name() = 0;
INTRINSICS_DO(V)
#undef V

 protected:
  Assembler* assembler() const { return assembler_; }

 private:
  Assembler* const assembler_;
};

void InterpreterGenerator::Generate() {
  GeneratePrologue();
  GenerateEpilogue();

#define V(name, branching, format, size, stack_diff, print)      \
  assembler()->Bind("BC_" #name);                                \
  Do##name();
  BYTECODES_DO(V)
#undef V

#define V(name)                          \
  assembler()->Bind("Intrinsic_" #name); \
  DoIntrinsic##name();
INTRINSICS_DO(V)
#undef V


  assembler()->BindWithPowerOfTwoAlignment("InterpretFast_DispatchTable", 4);
#define V(name, branching, format, size, stack_diff, print)      \
  assembler()->DefineLong("BC_" #name);
  BYTECODES_DO(V)
#undef V
}

class InterpreterGeneratorARM: public InterpreterGenerator {
 public:
  explicit InterpreterGeneratorARM(Assembler* assembler)
      : InterpreterGenerator(assembler) { }

  // Registers
  // ---------
  //   r4: current process
  //   r5: bytecode pointer
  //   r6: stack pointer (top)
  //   r8: null
  //   r10: true
  //   r11: false

  virtual void GeneratePrologue();
  virtual void GenerateEpilogue();

  virtual void DoLoadLocal0();
  virtual void DoLoadLocal1();
  virtual void DoLoadLocal2();
  virtual void DoLoadLocal();
  virtual void DoLoadLocalWide();

  virtual void DoLoadBoxed();
  virtual void DoLoadStatic();
  virtual void DoLoadStaticInit();
  virtual void DoLoadField();
  virtual void DoLoadFieldWide();

  virtual void DoLoadConst();
  virtual void DoLoadConstUnfold();

  virtual void DoStoreLocal();
  virtual void DoStoreBoxed();
  virtual void DoStoreStatic();
  virtual void DoStoreField();
  virtual void DoStoreFieldWide();

  virtual void DoLoadLiteralNull();
  virtual void DoLoadLiteralTrue();
  virtual void DoLoadLiteralFalse();
  virtual void DoLoadLiteral0();
  virtual void DoLoadLiteral1();
  virtual void DoLoadLiteral();
  virtual void DoLoadLiteralWide();

  virtual void DoInvokeMethod();
  virtual void DoInvokeMethodFast();
  virtual void DoInvokeMethodVtable();

  virtual void DoInvokeStatic();
  virtual void DoInvokeStaticUnfold();
  virtual void DoInvokeFactory();
  virtual void DoInvokeFactoryUnfold();

  virtual void DoInvokeNative();
  virtual void DoInvokeNativeYield();

  virtual void DoInvokeTest();
  virtual void DoInvokeTestFast();
  virtual void DoInvokeTestVtable();

  virtual void DoInvokeSelector();

#define INVOKE_BUILTIN(kind)                \
  virtual void DoInvoke##kind() {           \
    Invoke##kind("BC_InvokeMethod");        \
  }                                         \
  virtual void DoInvoke##kind##Fast() {     \
    Invoke##kind("BC_InvokeMethodFast");    \
  }                                         \
  virtual void DoInvoke##kind##Vtable() {   \
    Invoke##kind("BC_InvokeMethodVtable");  \
  }

  INVOKE_BUILTIN(Eq);
  INVOKE_BUILTIN(Lt);
  INVOKE_BUILTIN(Le);
  INVOKE_BUILTIN(Gt);
  INVOKE_BUILTIN(Ge);

  INVOKE_BUILTIN(Add);
  INVOKE_BUILTIN(Sub);
  INVOKE_BUILTIN(Mod);
  INVOKE_BUILTIN(Mul);
  INVOKE_BUILTIN(TruncDiv);

  INVOKE_BUILTIN(BitNot);
  INVOKE_BUILTIN(BitAnd);
  INVOKE_BUILTIN(BitOr);
  INVOKE_BUILTIN(BitXor);
  INVOKE_BUILTIN(BitShr);
  INVOKE_BUILTIN(BitShl);

#undef INVOKE_BUILTIN

  virtual void DoPop();
  virtual void DoReturn();
  virtual void DoReturnWide();

  virtual void DoBranchWide();
  virtual void DoBranchIfTrueWide();
  virtual void DoBranchIfFalseWide();

  virtual void DoBranchBack();
  virtual void DoBranchBackIfTrue();
  virtual void DoBranchBackIfFalse();

  virtual void DoBranchBackWide();
  virtual void DoBranchBackIfTrueWide();
  virtual void DoBranchBackIfFalseWide();

  virtual void DoPopAndBranchWide();
  virtual void DoPopAndBranchBackWide();

  virtual void DoAllocate();
  virtual void DoAllocateUnfold();
  virtual void DoAllocateImmutable();
  virtual void DoAllocateImmutableUnfold();
  virtual void DoAllocateBoxed();

  virtual void DoNegate();

  virtual void DoStackOverflowCheck();

  virtual void DoThrow();
  // Expects to be called after SaveState with the exception object in R7.
  virtual void DoThrowAfterSaveState();
  virtual void DoSubroutineCall();
  virtual void DoSubroutineReturn();

  virtual void DoProcessYield();
  virtual void DoCoroutineChange();

  virtual void DoIdentical();
  virtual void DoIdenticalNonNumeric();

  virtual void DoEnterNoSuchMethod();
  virtual void DoExitNoSuchMethod();

  virtual void DoFrameSize();
  virtual void DoMethodEnd();

  virtual void DoIntrinsicObjectEquals();
  virtual void DoIntrinsicGetField();
  virtual void DoIntrinsicSetField();
  virtual void DoIntrinsicListIndexGet();
  virtual void DoIntrinsicListIndexSet();
  virtual void DoIntrinsicListLength();

 private:
  Label done_;
  Label check_stack_overflow_;
  Label check_stack_overflow_0_;
  Label gc_;
  Label intrinsic_failure_;

  void LoadLocal(Register reg, int index);
  void StoreLocal(Register reg, int index);

  void Push(Register reg);
  void Pop(Register reg);
  void Drop(int n);

  void Return(bool wide);

  void Allocate(bool unfolded, bool immutable);

  // This function changes caller-saved registers.
  void AddToStoreBufferSlow(Register object, Register value);

  void InvokeEq(const char* fallback);
  void InvokeLt(const char* fallback);
  void InvokeLe(const char* fallback);
  void InvokeGt(const char* fallback);
  void InvokeGe(const char* fallback);
  void InvokeCompare(const char* fallback, Condition condition);

  void InvokeAdd(const char* fallback);
  void InvokeSub(const char* fallback);
  void InvokeMod(const char* fallback);
  void InvokeMul(const char* fallback);
  void InvokeTruncDiv(const char* fallback);

  void InvokeBitNot(const char* fallback);
  void InvokeBitAnd(const char* fallback);
  void InvokeBitOr(const char* fallback);
  void InvokeBitXor(const char* fallback);
  void InvokeBitShr(const char* fallback);
  void InvokeBitShl(const char* fallback);

  void InvokeMethod(bool test);
  void InvokeMethodFast(bool test);
  void InvokeMethodVtable(bool test);

  void InvokeNative(bool yield);
  void InvokeStatic(bool unfolded);

  void CheckStackOverflow(int size);

  void Dispatch(int size);

  void SaveState();
  void RestoreState();

  static int ComputeStackPadding(int reserved, int extra) {
    const int kAlignment = 8;
    int rounded = (reserved + extra + kAlignment - 1) & ~(kAlignment - 1);
    return rounded - reserved;
  }

  static RegisterList RegisterRange(Register first, Register last) {
    ASSERT(first <= last);
    RegisterList value = 0;
    for (int i = first; i <= last; i++) {
      value |= (1 << i);
    }
    return value;
  }
};

GENERATE(, InterpretFast) {
  InterpreterGeneratorARM generator(assembler);
  generator.Generate();
}

void InterpreterGeneratorARM::GeneratePrologue() {
  // Push callee-saved registers.
  __ push(RegisterRange(R4, R11) | RegisterRange(LR, LR));

  // Setup process pointer in R4.
  __ mov(R4, R0);

  // Pad the stack to gaurantee the right alignment for calls.
  int padding = ComputeStackPadding(9 * kWordSize, 1 * kWordSize);
  if (padding > 0) __ sub(SP, SP, Immediate(padding));

  // Store the argument target yield address in the extra slot on the
  // top of the stack.
  __ str(R1, Address(SP, 0));

  // Restore the register state and dispatch to the first bytecode.
  RestoreState();
  Dispatch(0);
}

void InterpreterGeneratorARM::GenerateEpilogue() {
  // Done. Save the register state.
  __ Bind(&done_);
  SaveState();

  // Undo stack padding.
  Label undo_padding;
  __ Bind(&undo_padding);
  int padding = ComputeStackPadding(9 * kWordSize, 1 * kWordSize);
  if (padding > 0) __ add(SP, SP, Immediate(padding));

  // Restore callee-saved registers and return.
  __ pop(RegisterRange(R4, R11) | RegisterRange(LR, LR));
  __ bx(LR);

  // Handle immutable heap allocation failures.
  Label immutable_alloc_failure;
  __ Bind(&immutable_alloc_failure);
  __ mov(R0, Immediate(Interpreter::kImmutableAllocationFailure));
  __ b(&undo_padding);

  // Handle GC and re-interpret current bytecode.
  __ Bind(&gc_);
  SaveState();
  __ mov(R0, R4);
  __ bl("HandleGC");
  __ tst(R0, R0);
  __ b(NE, &immutable_alloc_failure);
  RestoreState();
  Dispatch(0);

  // Stack overflow handling (slow case).
  Label stay_fast, overflow;
  __ Bind(&check_stack_overflow_0_);
  __ mov(R0, Immediate(0));
  __ Bind(&check_stack_overflow_);
  SaveState();

  __ mov(R1, R0);
  __ mov(R0, R4);
  __ bl("HandleStackOverflow");
  __ tst(R0, R0);
  ASSERT(Process::kStackCheckContinue == 0);
  __ b(EQ, &stay_fast);
  __ cmp(R0, Immediate(Process::kStackCheckInterrupt));
  __ b(NE, &overflow);
  __ mov(R0, Immediate(Interpreter::kInterrupt));
  __ b(&undo_padding);

  __ Bind(&stay_fast);
  RestoreState();
  Dispatch(0);

  __ Bind(&overflow);
  __ ldr(R7, Address(R4, Process::ProgramOffset()));
  __ ldr(R7, Address(R7, Program::raw_stack_overflow_offset()));
  DoThrowAfterSaveState();

  // Intrinsic failure: Just invoke the method.
  __ Bind(&intrinsic_failure_);
  __ add(R5, R5, Immediate(kInvokeMethodLength));
  Push(R5);
  __ add(R5, R0, Immediate(Function::kSize - HeapObject::kTag));
  Dispatch(0);
}

void InterpreterGeneratorARM::DoLoadLocal0() {
  LoadLocal(R0, 0);
  Push(R0);
  Dispatch(kLoadLocal0Length);
}

void InterpreterGeneratorARM::DoLoadLocal1() {
  LoadLocal(R0, 1);
  Push(R0);
  Dispatch(kLoadLocal1Length);
}

void InterpreterGeneratorARM::DoLoadLocal2() {
  LoadLocal(R0, 2);
  Push(R0);
  Dispatch(kLoadLocal2Length);
}

void InterpreterGeneratorARM::DoLoadLocal() {
  __ ldrb(R0, Address(R5, 1));
  __ neg(R1, R0);
  __ ldr(R0, Address(R6, Operand(R1, TIMES_4)));
  Push(R0);
  Dispatch(kLoadLocalLength);
}

void InterpreterGeneratorARM::DoLoadLocalWide() {
  __ ldr(R0, Address(R5, 1));
  __ neg(R1, R0);
  __ ldr(R0, Address(R6, Operand(R1, TIMES_4)));
  Push(R0);
  Dispatch(kLoadLocalWideLength);
}

void InterpreterGeneratorARM::DoLoadBoxed() {
  __ ldrb(R0, Address(R5, 1));
  __ neg(R0, R0);
  __ ldr(R1, Address(R6, Operand(R0, TIMES_4)));
  __ ldr(R0, Address(R1, Boxed::kValueOffset - HeapObject::kTag));
  Push(R0);
  Dispatch(kLoadBoxedLength);
}

void InterpreterGeneratorARM::DoLoadStatic() {
  __ ldr(R0, Address(R5, 1));
  __ ldr(R1, Address(R4, Process::StaticsOffset()));
  __ add(R1, R1, Immediate(Array::kSize - HeapObject::kTag));
  __ ldr(R0, Address(R1, Operand(R0, TIMES_4)));
  Push(R0);
  Dispatch(kLoadStaticLength);
}

void InterpreterGeneratorARM::DoLoadStaticInit() {
  __ ldr(R0, Address(R5, 1));
  __ ldr(R1, Address(R4, Process::StaticsOffset()));
  __ add(R1, R1, Immediate(Array::kSize - HeapObject::kTag));
  __ ldr(R0, Address(R1, Operand(R0, TIMES_4)));

  Label done;
  ASSERT(Smi::kTag == 0);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(EQ, &done);
  __ ldr(R1, Address(R0, HeapObject::kClassOffset - HeapObject::kTag));
  __ ldr(R1, Address(R1, Class::kInstanceFormatOffset - HeapObject::kTag));

  int type = InstanceFormat::INITIALIZER_TYPE;
  __ and_(R1, R1, Immediate(InstanceFormat::TypeField::mask()));
  __ cmp(R1, Immediate(type << InstanceFormat::TypeField::shift()));
  __ b(NE, &done);

  // Invoke the initializer function.
  __ ldr(R0, Address(R0, Initializer::kFunctionOffset - HeapObject::kTag));
  __ add(R5, R5, Immediate(kInvokeMethodLength));
  Push(R5);

  // Jump to the first bytecode in the initializer function.
  __ add(R5, R0, Immediate(Function::kSize - HeapObject::kTag));
  CheckStackOverflow(0);
  Dispatch(0);

  __ Bind(&done);
  Push(R0);
  Dispatch(kLoadStaticInitLength);
}

void InterpreterGeneratorARM::DoLoadField() {
  __ ldrb(R1, Address(R5, 1));
  LoadLocal(R0, 0);
  __ add(R0, R0, Immediate(Instance::kSize - HeapObject::kTag));
  __ ldr(R0, Address(R0, Operand(R1, TIMES_4)));
  StoreLocal(R0, 0);
  Dispatch(kLoadFieldLength);
}

void InterpreterGeneratorARM::DoLoadFieldWide() {
  __ ldr(R1, Address(R5, 1));
  LoadLocal(R0, 0);
  __ add(R0, R0, Immediate(Instance::kSize - HeapObject::kTag));
  __ ldr(R0, Address(R0, Operand(R1, TIMES_4)));
  StoreLocal(R0, 0);
  Dispatch(kLoadFieldWideLength);
}

void InterpreterGeneratorARM::DoLoadConst() {
  __ ldr(R0, Address(R5, 1));
  __ ldr(R1, Address(R4, Process::ProgramOffset()));
  __ ldr(R2, Address(R1, Program::ConstantsOffset()));
  __ add(R2, R2, Immediate(Array::kSize - HeapObject::kTag));
  __ ldr(R3, Address(R2, Operand(R0, TIMES_4)));
  Push(R3);
  Dispatch(kLoadConstLength);
}

void InterpreterGeneratorARM::DoLoadConstUnfold() {
  __ ldr(R0, Address(R5, 1));
  __ ldr(R2, Address(R5, Operand(R0, TIMES_1)));
  Push(R2);
  Dispatch(kLoadConstUnfoldLength);
}

void InterpreterGeneratorARM::DoStoreLocal() {
  LoadLocal(R1, 0);
  __ ldrb(R0, Address(R5, 1));
  __ neg(R0, R0);
  __ str(R1, Address(R6, Operand(R0, TIMES_4)));
  Dispatch(kStoreLocalLength);
}

void InterpreterGeneratorARM::DoStoreBoxed() {
  LoadLocal(R2, 0);
  __ ldrb(R0, Address(R5, 1));
  __ neg(R0, R0);
  __ ldr(R1, Address(R6, Operand(R0, TIMES_4)));
  __ str(R2, Address(R1, Boxed::kValueOffset - HeapObject::kTag));

  AddToStoreBufferSlow(R1, R2);

  Dispatch(kStoreBoxedLength);
}

void InterpreterGeneratorARM::DoStoreStatic() {
  LoadLocal(R2, 0);
  __ ldr(R0, Address(R5, 1));
  __ ldr(R1, Address(R4, Process::StaticsOffset()));
  __ add(R3, R1, Immediate(Array::kSize - HeapObject::kTag));
  __ str(R2, Address(R3, Operand(R0, TIMES_4)));

  AddToStoreBufferSlow(R1, R2);

  Dispatch(kStoreStaticLength);
}

void InterpreterGeneratorARM::DoStoreField() {
  __ ldrb(R1, Address(R5, 1));
  LoadLocal(R2, 0);
  LoadLocal(R0, 1);
  __ add(R3, R0, Immediate(Instance::kSize - HeapObject::kTag));
  __ str(R2, Address(R3, Operand(R1, TIMES_4)));
  StoreLocal(R2, 1);
  Drop(1);

  AddToStoreBufferSlow(R0, R2);

  Dispatch(kStoreFieldLength);
}

void InterpreterGeneratorARM::DoStoreFieldWide() {
  __ ldr(R1, Address(R5, 1));
  LoadLocal(R2, 0);
  LoadLocal(R0, 1);
  __ add(R3, R0, Immediate(Instance::kSize - HeapObject::kTag));
  __ str(R2, Address(R3, Operand(R1, TIMES_4)));
  StoreLocal(R2, 1);
  Drop(1);

  AddToStoreBufferSlow(R0, R2);

  Dispatch(kStoreFieldWideLength);
}

void InterpreterGeneratorARM::DoLoadLiteralNull() {
  Push(R8);
  Dispatch(kLoadLiteralNullLength);
}

void InterpreterGeneratorARM::DoLoadLiteralTrue() {
  Push(R10);
  Dispatch(kLoadLiteralTrueLength);
}

void InterpreterGeneratorARM::DoLoadLiteralFalse() {
  Push(R11);
  Dispatch(kLoadLiteralFalseLength);
}

void InterpreterGeneratorARM::DoLoadLiteral0() {
  __ mov(R0, Immediate(reinterpret_cast<int32_t>(Smi::FromWord(0))));
  Push(R0);
  Dispatch(kLoadLiteral0Length);
}

void InterpreterGeneratorARM::DoLoadLiteral1() {
  __ mov(R0, Immediate(reinterpret_cast<int32_t>(Smi::FromWord(1))));
  Push(R0);
  Dispatch(kLoadLiteral1Length);
}

void InterpreterGeneratorARM::DoLoadLiteral() {
  __ ldrb(R0, Address(R5, 1));
  __ lsl(R0, R0, Immediate(Smi::kTagSize));
  ASSERT(Smi::kTag == 0);
  Push(R0);
  Dispatch(kLoadLiteralLength);
}

void InterpreterGeneratorARM::DoLoadLiteralWide() {
  ASSERT(Smi::kTag == 0);
  __ ldr(R0, Address(R5, 1));
  __ lsl(R0, R0, Immediate(Smi::kTagSize));
  Push(R0);
  Dispatch(kLoadLiteralWideLength);
}

void InterpreterGeneratorARM::DoInvokeMethod() {
  InvokeMethod(false);
}

void InterpreterGeneratorARM::DoInvokeMethodFast() {
  InvokeMethodFast(false);
}

void InterpreterGeneratorARM::DoInvokeMethodVtable() {
  InvokeMethodVtable(false);
}

void InterpreterGeneratorARM::DoInvokeTest() {
  InvokeMethod(true);
}

void InterpreterGeneratorARM::DoInvokeTestFast() {
  InvokeMethodFast(true);
}

void InterpreterGeneratorARM::DoInvokeTestVtable() {
  InvokeMethodVtable(true);
}

void InterpreterGeneratorARM::DoInvokeStatic() {
  InvokeStatic(false);
}

void InterpreterGeneratorARM::DoInvokeStaticUnfold() {
  InvokeStatic(true);
}

void InterpreterGeneratorARM::DoInvokeFactory() {
  InvokeStatic(false);
}

void InterpreterGeneratorARM::DoInvokeFactoryUnfold() {
  InvokeStatic(true);
}

void InterpreterGeneratorARM::DoInvokeNative() {
  InvokeNative(false);
}

void InterpreterGeneratorARM::DoInvokeNativeYield() {
  InvokeNative(true);
}

void InterpreterGeneratorARM::DoInvokeSelector() {
  SaveState();
  __ mov(R0, R4);
  __ bl("HandleInvokeSelector");
  RestoreState();
  CheckStackOverflow(0);
  Dispatch(0);
}

void InterpreterGeneratorARM::InvokeEq(const char* fallback) {
  InvokeCompare(fallback, EQ);
}

void InterpreterGeneratorARM::InvokeLt(const char* fallback) {
  InvokeCompare(fallback, LT);
}

void InterpreterGeneratorARM::InvokeLe(const char* fallback) {
  InvokeCompare(fallback, LE);
}

void InterpreterGeneratorARM::InvokeGt(const char* fallback) {
  InvokeCompare(fallback, GT);
}

void InterpreterGeneratorARM::InvokeGe(const char* fallback) {
  InvokeCompare(fallback, GE);
}

void InterpreterGeneratorARM::InvokeAdd(const char* fallback) {
  LoadLocal(R0, 1);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);
  LoadLocal(R1, 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, fallback);

  __ adds(R0, R0, R1);
  __ b(VS, fallback);
  StoreLocal(R0, 1);
  Drop(1);
  Dispatch(kInvokeAddLength);
}

void InterpreterGeneratorARM::InvokeSub(const char* fallback) {
  LoadLocal(R0, 1);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);
  LoadLocal(R1, 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, fallback);

  __ subs(R0, R0, R1);
  __ b(VS, fallback);
  StoreLocal(R0, 1);
  Drop(1);
  Dispatch(kInvokeAddLength);
}

void InterpreterGeneratorARM::InvokeMod(const char* fallback) {
  // TODO(ager): Implement. Probably need to go to floating-point
  // arithmetic for this on arm.
  __ b(fallback);
}

void InterpreterGeneratorARM::InvokeMul(const char* fallback) {
  LoadLocal(R0, 1);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);
  LoadLocal(R1, 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, fallback);

  // Untag one of the arguments, multiply, and check for overflow.
  // The overflow check is complicated on arm. We use smull to
  // produce a 64-bit result with the high 32 bit in IP and the
  // low in R0. We then check that the high 33 bit are all equal
  // which is the overflow check.
  __ asr(R0, R0, Immediate(1));
  __ smull(R0, IP, R1, R0);
  __ cmp(IP, Operand(R0, ASR, 31));
  __ b(NE, fallback);

  StoreLocal(R0, 1);
  Drop(1);
  Dispatch(kInvokeMulLength);
}

void InterpreterGeneratorARM::InvokeTruncDiv(const char* fallback) {
  // TODO(ager): Do this using floating point instruction and registers.
  __ b(fallback);
}

void InterpreterGeneratorARM::InvokeBitNot(const char* fallback) {
  LoadLocal(R0, 0);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);

  // Move negated.
  __ mvn(R1, R0);
  // Bit clear the smi tag bit to smi tag again.
  __ bic(R1, R1, Immediate(Smi::kTagMask));

  StoreLocal(R1, 0);
  Dispatch(kInvokeBitNotLength);
}

void InterpreterGeneratorARM::InvokeBitAnd(const char* fallback) {
  LoadLocal(R0, 1);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);
  LoadLocal(R1, 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, fallback);

  __ and_(R0, R0, R1);
  StoreLocal(R0, 1);
  Drop(1);
  Dispatch(kInvokeBitAndLength);
}

void InterpreterGeneratorARM::InvokeBitOr(const char* fallback) {
  LoadLocal(R0, 1);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);
  LoadLocal(R1, 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, fallback);

  __ orr(R0, R0, R1);
  StoreLocal(R0, 1);
  Drop(1);
  Dispatch(kInvokeBitAndLength);
}

void InterpreterGeneratorARM::InvokeBitXor(const char* fallback) {
  LoadLocal(R0, 1);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);
  LoadLocal(R1, 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, fallback);

  __ eor(R0, R0, R1);
  StoreLocal(R0, 1);
  Drop(1);
  Dispatch(kInvokeBitXorLength);
}

void InterpreterGeneratorARM::InvokeBitShr(const char* fallback) {
  LoadLocal(R0, 1);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);
  LoadLocal(R1, 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, fallback);

  // Untag and shift.
  __ asr(R0, R0, Immediate(1));
  __ asr(R1, R1, Immediate(1));
  __ asr(R0, R0, R1);

  // Retag and store.
  __ add(R0, R0, R0);
  StoreLocal(R0, 1);
  Drop(1);
  Dispatch(kInvokeBitAndLength);
}

void InterpreterGeneratorARM::InvokeBitShl(const char* fallback) {
  LoadLocal(R0, 1);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);
  LoadLocal(R1, 0);
  __ tst(R1, Immediate(Smi::kTagSize));
  __ b(NE, fallback);

  // Untag the shift count, but not the value. If the shift
  // count is greater than 31 (or negative), the shift is going
  // to misbehave so we have to guard against that.
  __ asr(R1, R1, Immediate(1));
  __ cmp(R1, Immediate(31));
  __ b(HI, fallback);

  // Only allow to shift out "sign bits". If we shift
  // out any other bit, it's an overflow.
  __ lsl(R2, R0, R1);
  __ asr(R3, R2, R1);
  __ cmp(R3, R0);
  __ b(NE, fallback);

  StoreLocal(R2, 1);
  Drop(1);
  Dispatch(kInvokeBitShlLength);
}

void InterpreterGeneratorARM::DoPop() {
  Drop(1);
  Dispatch(kPopLength);
}

void InterpreterGeneratorARM::DoReturn() {
  Return(false);
}

void InterpreterGeneratorARM::DoReturnWide() {
  Return(true);
}

void InterpreterGeneratorARM::DoBranchWide() {
  __ ldr(R0, Address(R5, 1));
  __ add(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoBranchIfTrueWide() {
  Label branch;
  Pop(R7);
  __ cmp(R7, R10);
  __ b(EQ, &branch);
  Dispatch(kBranchIfTrueWideLength);

  __ Bind(&branch);
  __ ldr(R0, Address(R5, 1));
  __ add(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoBranchIfFalseWide() {
  Label branch;
  Pop(R7);
  __ cmp(R7, R10);
  __ b(NE, &branch);
  Dispatch(kBranchIfFalseWideLength);

  __ Bind(&branch);
  __ ldr(R0, Address(R5, 1));
  __ add(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoBranchBack() {
  CheckStackOverflow(0);
  __ ldrb(R0, Address(R5, 1));
  __ sub(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoBranchBackIfTrue() {
  CheckStackOverflow(0);

  Label branch;
  Pop(R1);
  __ cmp(R1, R10);
  __ b(EQ, &branch);
  Dispatch(kBranchBackIfTrueLength);

  __ Bind(&branch);
  __ ldrb(R0, Address(R5, 1));
  __ sub(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoBranchBackIfFalse() {
  CheckStackOverflow(0);

  Label branch;
  Pop(R1);
  __ cmp(R1, R10);
  __ b(NE, &branch);
  Dispatch(kBranchBackIfFalseLength);

  __ Bind(&branch);
  __ ldrb(R0, Address(R5, 1));
  __ sub(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoBranchBackWide() {
  CheckStackOverflow(0);
  __ ldr(R0, Address(R5, 1));
  __ sub(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoBranchBackIfTrueWide() {
  CheckStackOverflow(0);

  Label branch;
  Pop(R1);
  __ cmp(R10, R1);
  __ b(EQ, &branch);
  Dispatch(kBranchBackIfTrueWideLength);

  __ Bind(&branch);
  __ ldr(R0, Address(R5, 1));
  __ sub(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoBranchBackIfFalseWide() {
  CheckStackOverflow(0);

  Label branch;
  Pop(R1);
  __ cmp(R10, R1);
  __ b(NE, &branch);
  Dispatch(kBranchBackIfTrueWideLength);

  __ Bind(&branch);
  __ ldr(R0, Address(R5, 1));
  __ sub(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoPopAndBranchWide() {
  __ ldrb(R0, Address(R5, 1));
  __ sub(R6, R6, Operand(R0, TIMES_4));

  __ ldr(R0, Address(R5, 2));
  __ add(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoPopAndBranchBackWide() {
  CheckStackOverflow(0);

  __ ldrb(R0, Address(R5, 1));
  __ sub(R6, R6, Operand(R0, TIMES_4));

  __ ldr(R0, Address(R5, 2));
  __ sub(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoAllocate() {
  Allocate(false, false);
}

void InterpreterGeneratorARM::DoAllocateUnfold() {
  Allocate(true, false);
}

void InterpreterGeneratorARM::DoAllocateImmutable() {
  Allocate(false, true);
}

void InterpreterGeneratorARM::DoAllocateImmutableUnfold() {
  Allocate(true, true);
}

void InterpreterGeneratorARM::DoAllocateBoxed() {
  LoadLocal(R1, 0);
  __ mov(R0, R4);
  __ bl("HandleAllocateBoxed");
  __ cmp(R0, Immediate(reinterpret_cast<int32>(Failure::retry_after_gc())));
  __ b(EQ, &gc_);
  StoreLocal(R0, 0);
  Dispatch(kAllocateBoxedLength);
}

void InterpreterGeneratorARM::DoNegate() {
  LoadLocal(R1, 0);
  __ cmp(R1, R10);
  __ str(EQ, R11, Address(R6, 0));
  __ str(NE, R10, Address(R6, 0));
  Dispatch(kNegateLength);
}

void InterpreterGeneratorARM::DoStackOverflowCheck() {
  __ ldr(R0, Address(R5, 1));
  __ ldr(R1, Address(R4, Process::StackLimitOffset()));
  __ add(R3, R6, Operand(R0, TIMES_4));
  __ cmp(R1, R3);
  __ b(LS, &check_stack_overflow_);
  Dispatch(kStackOverflowCheckLength);
}

void InterpreterGeneratorARM::DoThrowAfterSaveState() {
  // Use the stack to store the stack delta initialized to zero.
  __ sub(SP, SP, Immediate(8));
  __ add(R2, SP, Immediate(kWordSize));
  __ mov(R3, Immediate(0));
  __ str(R3, Address(R2, 0));

  __ mov(R0, R4);
  __ mov(R1, R7);
  __ bl("HandleThrow");

  RestoreState();

  __ ldr(R3, Address(SP, kWordSize));
  __ add(SP, SP, Immediate(8));

  Label unwind;
  __ tst(R0, R0);
  __ b(NE, &unwind);
  __ mov(R0, Immediate(Interpreter::kUncaughtException));
  __ b(&done_);

  __ Bind(&unwind);
  __ neg(R3, R3);
  __ mov(R5, R0);
  __ add(R6, R6, Operand(R3, TIMES_4));
  __ add(R6, R6, Immediate(kWordSize));

  StoreLocal(R7, 0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoThrow() {
  // Load object into callee-save register not touched by
  // save and restore state.
  LoadLocal(R7, 0);
  SaveState();
  DoThrowAfterSaveState();
}

void InterpreterGeneratorARM::DoSubroutineCall() {
  __ ldr(R0, Address(R5, 1));
  __ ldr(R1, Address(R5, 5));

  // Push the return delta as a tagged smi.
  ASSERT(Smi::kTag == 0);
  __ lsl(R1, R1, Immediate(Smi::kTagSize));
  Push(R1);

  __ add(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoSubroutineReturn() {
  Pop(R0);
  __ lsr(R0, R0, Immediate(Smi::kTagSize));
  __ sub(R5, R5, R0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoProcessYield() {
  LoadLocal(R0, 0);
  __ asr(R0, R0, Immediate(1));
  __ add(R5, R5, Immediate(kProcessYieldLength));
  StoreLocal(R8, 0);
  __ b(&done_);
}

void InterpreterGeneratorARM::DoCoroutineChange() {
  // Load argument into callee-saved register not touched by
  // SaveState and RestoreState.
  LoadLocal(R7, 0);
  // Load coroutine.
  LoadLocal(R1, 1);

  // Store null in locals.
  StoreLocal(R8, 0);
  StoreLocal(R8, 1);

  // Perform call preserving argument in R7.
  SaveState();
  __ mov(R0, R4);
  __ bl("HandleCoroutineChange");
  RestoreState();

  // Store argument.
  StoreLocal(R7, 1);
  Drop(1);

  Dispatch(kCoroutineChangeLength);
}

void InterpreterGeneratorARM::DoIdentical() {
  LoadLocal(R0, 0);
  LoadLocal(R1, 1);

  // TODO(ager): For now we bail out if we have two doubles or two
  // large integers and let the slow interpreter deal with it. These
  // cases could be dealt with directly here instead.
  Label fast_case;
  Label bail_out;

  // If either is a smi they are not both doubles or large integers.
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(EQ, &fast_case);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(EQ, &fast_case);

  // If they do not have the same type they are not both double or
  // large integers.
  __ ldr(R2, Address(R0, HeapObject::kClassOffset - HeapObject::kTag));
  __ ldr(R2, Address(R2, Class::kInstanceFormatOffset - HeapObject::kTag));
  __ ldr(R3, Address(R1, HeapObject::kClassOffset - HeapObject::kTag));
  __ ldr(R3, Address(R3, Class::kInstanceFormatOffset - HeapObject::kTag));
  __ cmp(R2, R3);
  __ b(NE, &fast_case);

  int double_type = InstanceFormat::DOUBLE_TYPE;
  int large_integer_type = InstanceFormat::LARGE_INTEGER_TYPE;
  int type_field_shift = InstanceFormat::TypeField::shift();

  __ and_(R2, R2, Immediate(InstanceFormat::TypeField::mask()));
  __ cmp(R2, Immediate(double_type << type_field_shift));
  __ b(EQ, &bail_out);
  __ cmp(R2, Immediate(large_integer_type << type_field_shift));
  __ b(EQ, &bail_out);

  __ Bind(&fast_case);
  __ cmp(R1, R0);
  __ str(EQ, R10, Address(R6, -kWordSize));
  __ str(NE, R11, Address(R6, -kWordSize));
  Drop(1);
  Dispatch(kIdenticalLength);

  __ Bind(&bail_out);
  __ mov(R2, R0);
  __ mov(R0, R4);
  __ bl("HandleIdentical");
  StoreLocal(R0, 1);
  Drop(1);
  Dispatch(kIdenticalLength);
}

void InterpreterGeneratorARM::DoIdenticalNonNumeric() {
  LoadLocal(R0, 0);
  LoadLocal(R1, 1);
  __ cmp(R0, R1);
  __ str(EQ, R10, Address(R6, -kWordSize));
  __ str(NE, R11, Address(R6, -kWordSize));
  Drop(1);
  Dispatch(kIdenticalNonNumericLength);
}

void InterpreterGeneratorARM::DoEnterNoSuchMethod() {
  SaveState();
  __ mov(R0, R4);
  __ bl("HandleEnterNoSuchMethod");
  RestoreState();
  Dispatch(0);
}

void InterpreterGeneratorARM::DoExitNoSuchMethod() {
  Pop(R0);  // Result.
  Pop(R1);  // Selector.
  __ lsr(R1, R1, Immediate(Smi::kTagSize));
  Drop(1);  // Sentinel.
  Pop(R5);

  Label done;
  __ and_(R2, R1, Immediate(Selector::KindField::mask()));
  __ cmp(R2, Immediate(Selector::SETTER << Selector::KindField::shift()));
  __ b(NE, &done);
  LoadLocal(R0, 0);

  __ Bind(&done);
  ASSERT(Selector::ArityField::shift() == 0);
  __ and_(R1, R1, Immediate(Selector::ArityField::mask()));
  __ neg(R1, R1);

  // Drop the arguments from the stack, but leave the receiver.
  __ add(R6, R6, Operand(R1, TIMES_4));

  StoreLocal(R0, 0);
  Dispatch(0);
}

void InterpreterGeneratorARM::DoFrameSize() {
  __ bkpt();
}

void InterpreterGeneratorARM::DoMethodEnd() {
  __ bkpt();
}

void InterpreterGeneratorARM::DoIntrinsicObjectEquals() {
  LoadLocal(R0, 0);
  LoadLocal(R1, 1);
  __ cmp(R0, R1);
  __ str(EQ, R10, Address(R6, -kWordSize));
  __ str(NE, R11, Address(R6, -kWordSize));
  Drop(1);
  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorARM::DoIntrinsicGetField() {
  __ ldrb(R1, Address(R0, 2 + Function::kSize - HeapObject::kTag));
  LoadLocal(R0, 0);
  __ add(R0, R0, Immediate(Instance::kSize - HeapObject::kTag));
  __ ldr(R0, Address(R0, Operand(R1, TIMES_4)));
  StoreLocal(R0, 0);
  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorARM::DoIntrinsicSetField() {
  __ ldrb(R1, Address(R0, 3 + Function::kSize - HeapObject::kTag));
  LoadLocal(R0, 0);
  LoadLocal(R2, 1);
  __ add(R3, R2, Immediate(Instance::kSize - HeapObject::kTag));
  __ str(R0, Address(R3, Operand(R1, TIMES_4)));
  StoreLocal(R0, 1);
  Drop(1);

  AddToStoreBufferSlow(R2, R0);

  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorARM::DoIntrinsicListIndexGet() {
  LoadLocal(R1, 0);  // Index.
  LoadLocal(R2, 1);  // List.

  Label failure;
  ASSERT(Smi::kTag == 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, &intrinsic_failure_);
  __ cmp(R1, Immediate(0));
  __ b(LT, &intrinsic_failure_);

  // Load the backing store (array) from the first instance field of the list.
  __ ldr(R2, Address(R2, Instance::kSize - HeapObject::kTag));
  __ ldr(R3, Address(R2, Array::kLengthOffset - HeapObject::kTag));

  // Check the index against the length.
  __ cmp(R1, R3);
  __ b(GE, &intrinsic_failure_);

  // Load from the array and continue.
  ASSERT(Smi::kTagSize == 1);
  __ add(R2, R2, Immediate(Array::kSize - HeapObject::kTag));
  __ ldr(R0, Address(R2, Operand(R1, TIMES_2)));
  StoreLocal(R0, 1);
  Drop(1);
  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorARM::DoIntrinsicListIndexSet() {
  LoadLocal(R1, 1);  // Index.
  LoadLocal(R2, 2);  // List.

  ASSERT(Smi::kTag == 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, &intrinsic_failure_);
  __ cmp(R1, Immediate(0));
  __ b(LT, &intrinsic_failure_);

  // Load the backing store (array) from the first instance field of the list.
  __ ldr(R2, Address(R2, Instance::kSize - HeapObject::kTag));
  __ ldr(R3, Address(R2, Array::kLengthOffset - HeapObject::kTag));

  // Check the index against the length.
  __ cmp(R1, R3);
  __ b(GE, &intrinsic_failure_);

  // Store to the array and continue.
  ASSERT(Smi::kTagSize == 1);
  LoadLocal(R0, 0);
  __ add(R12, R2, Immediate(Array::kSize - HeapObject::kTag));
  __ str(R0, Address(R12, Operand(R1, TIMES_2)));
  StoreLocal(R0, 2);
  Drop(2);

  AddToStoreBufferSlow(R2, R0);

  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorARM::DoIntrinsicListLength() {
  // Load the backing store (array) from the first instance field of the list.
  LoadLocal(R2, 0);  // List.
  __ ldr(R2, Address(R2, Instance::kSize - HeapObject::kTag));
  __ ldr(R3, Address(R2, Array::kLengthOffset - HeapObject::kTag));
  StoreLocal(R3, 0);
  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorARM::Push(Register reg) {
  StoreLocal(reg, -1);
  __ add(R6, R6, Immediate(1 * kWordSize));
}

void InterpreterGeneratorARM::Pop(Register reg) {
  LoadLocal(reg, 0);
  Drop(1);
}

void InterpreterGeneratorARM::Return(bool wide) {
  // Get result from stack.
  LoadLocal(R0, 0);

  // Fetch the number of locals and arguments from the bytecodes.
  // Unfortunately, we have to negate the counts so we can use them
  // to index into the stack (grows towards higher addresses).
  if (wide) {
    __ ldr(R1, Address(R5, 1));
    __ ldrb(R2, Address(R5, 5));
  } else {
    __ ldrb(R1, Address(R5, 1));
    __ ldrb(R2, Address(R5, 2));
  }
  __ neg(R1, R1);

  // Load the return address.
  __ ldr(R5, Address(R6, Operand(R1, TIMES_4)));

  // Drop both locals and arguments except one which we will overwrite
  // with the result (we've left the return address on the stack).
  __ sub(R1, R1, R2);
  __ add(R6, R6, Operand(R1, TIMES_4));

  // Overwrite the first argument (or the return address) with the result
  // and dispatch to the next bytecode.
  StoreLocal(R0, 0);
  Dispatch(0);
}

void InterpreterGeneratorARM::LoadLocal(Register reg, int index) {
  __ ldr(reg, Address(R6, -index * kWordSize));
}

void InterpreterGeneratorARM::StoreLocal(Register reg, int index) {
  __ str(reg, Address(R6, -index * kWordSize));
}

void InterpreterGeneratorARM::Drop(int n) {
  __ sub(R6, R6, Immediate(n * kWordSize));
}

void InterpreterGeneratorARM::InvokeMethod(bool test) {
  // Get the selector from the bytecodes.
  __ ldr(R7, Address(R5, 1));

  if (test) {
    // Get the receiver from the stack.
    LoadLocal(R1, 0);
  } else {
    // Compute the arity from the selector.
    ASSERT(Selector::ArityField::shift() == 0);
    __ and_(R2, R7, Immediate(Selector::ArityField::mask()));

    // Get the receiver from the stack.
    __ neg(R3, R2);
    __ ldr(R1, Address(R6, Operand(R3, TIMES_4)));
  }

  // Compute the receiver class.
  Label smi, probe;
  ASSERT(Smi::kTag == 0);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(EQ, &smi);
  __ ldr(R2, Address(R1, HeapObject::kClassOffset - HeapObject::kTag));

  // Find the entry in the primary lookup cache.
  Label miss, finish;
  ASSERT(Utils::IsPowerOfTwo(LookupCache::kPrimarySize));
  ASSERT(sizeof(LookupCache::Entry) == 1 << 4);
  __ Bind(&probe);
  __ eor(R3, R2, R7);
  __ ldr(R0, Immediate(LookupCache::kPrimarySize - 1));
  __ and_(R0, R3, R0);
  __ ldr(R3, Address(R4, Process::PrimaryLookupCacheOffset()));
  __ add(R0, R3, Operand(R0, LSL, 4));

  // Validate the primary entry.
  __ ldr(R3, Address(R0, OFFSET_OF(LookupCache::Entry, clazz)));
  __ cmp(R2, R3);
  __ b(NE, &miss);
  __ ldr(R3, Address(R0, OFFSET_OF(LookupCache::Entry, selector)));
  __ cmp(R7, R3);
  __ b(NE, &miss);

  // At this point, we've got our hands on a valid lookup cache entry.
  Label intrinsified;
  __ Bind(&finish);
  if (test) {
    __ ldr(R0, Address(R0, OFFSET_OF(LookupCache::Entry, tag)));
  } else {
    __ ldr(R7, Address(R0, OFFSET_OF(LookupCache::Entry, tag)));
    __ ldr(R0, Address(R0, OFFSET_OF(LookupCache::Entry, target)));
    __ cmp(R7, Immediate(1));
    __ b(HI, &intrinsified);
  }

  if (test) {
    // Materialize either true or false depending on whether or not
    // we've found a target method.
    Label found;
    __ tst(R0, R0);
    __ str(EQ, R11, Address(R6, 0));
    __ str(NE, R10, Address(R6, 0));
    Dispatch(kInvokeTestLength);
  } else {
    // Compute and push the return address on the stack.
    __ add(R5, R5, Immediate(kInvokeMethodLength));
    Push(R5);

    // Jump to the first bytecode in the target method.
    __ add(R5, R0, Immediate(Function::kSize - HeapObject::kTag));
    CheckStackOverflow(0);
    Dispatch(0);
  }

  __ Bind(&smi);
  __ ldr(R3, Address(R4, Process::ProgramOffset()));
  __ ldr(R2, Address(R3, Program::smi_class_offset()));
  __ b(&probe);

  if (!test) {
    __ Bind(&intrinsified);
    __ mov(PC, R7);
  }

  // We didn't find a valid entry in primary lookup cache.
  __ Bind(&miss);
  // Arguments:
  // - r0: process
  // - r1: primary cache entry
  // - r2: class (already in r2)
  // - r3: selector
  __ mov(R1, R0);
  __ mov(R0, R4);
  __ mov(R3, R7);
  __ bl("HandleLookupEntry");
  __ b(&finish);
}

void InterpreterGeneratorARM::InvokeMethodFast(bool test) {
  // Get the dispatch table and form a pointer to the first element
  // corresponding to this invoke bytecode.
  __ ldr(R7, Address(R5, 1));
  __ ldr(R1, Address(R4, Process::ProgramOffset()));
  __ ldr(R2, Address(R1, Program::DispatchTableOffset()));
  __ add(R3, R2, Immediate(Array::kSize - HeapObject::kTag));
  __ add(R7, R3, Operand(R7, TIMES_4));

  // Get the receiver from the stack.
  if (test) {
    LoadLocal(R2, 0);
  } else {
    __ ldr(R2, Address(R7, 0));
    __ neg(R2, R2);
    __ ldr(R2, Address(R6, Operand(R2, TIMES_2)));
  }

  // Compute the receiver class.
  Label smi, probe;
  ASSERT(Smi::kTag == 0);
  __ tst(R2, Immediate(Smi::kTagMask));
  __ b(EQ, &smi);
  __ ldr(R2, Address(R2, HeapObject::kClassOffset - HeapObject::kTag));

  // Fetch the receiver class id and get ready to look at the table entries.
  int id_offset = Class::kIdOrTransformationTargetOffset - HeapObject::kTag;
  __ Bind(&probe);
  __ ldr(R2, Address(R2, id_offset));

  // Loop through the table.
  Label loop, next;
  __ Bind(&loop);
  __ ldr(R9, Address(R7, 4 * kPointerSize));
  __ cmp(R2, R9);
  __ b(LT, &next);
  __ ldr(R9, Address(R7, 5 * kPointerSize));
  __ cmp(R2, R9);
  __ b(GE, &next);

  Label intrinsified;
  if (test) {
    const int32 kMax = reinterpret_cast<int32>(
        Smi::FromWord(Smi::kMaxPortableValue));
    __ cmp(R9, Immediate(kMax));
    __ str(EQ, R11, Address(R6, 0));  // Store false.
    __ str(NE, R10, Address(R6, 0));  // Store true.
    Dispatch(kInvokeTestLength);
  } else {
    // Found the right target method.
    __ ldr(R2, Address(R7, 6 * kPointerSize));
    __ ldr(R0, Address(R7, 7 * kPointerSize));
    __ tst(R2, R2);
    __ b(NE, &intrinsified);

    // Compute and push the return address on the stack.
    __ add(R5, R5, Immediate(kInvokeMethodFastLength));
    Push(R5);

    // Jump to the first bytecode in the target method.
    __ add(R5, R0, Immediate(Function::kSize - HeapObject::kTag));
    CheckStackOverflow(0);
    Dispatch(0);
  }

  // Go to the next table entry.
  __ Bind(&next);
  __ add(R7, R7, Immediate(4 * kPointerSize));
  __ b(&loop);

  if (!test) {
    __ Bind(&intrinsified);
    __ mov(PC, R2);
  }

  __ Bind(&smi);
  __ ldr(R2, Address(R1, Program::smi_class_offset()));
  __ b(&probe);
}

void InterpreterGeneratorARM::InvokeMethodVtable(bool test) {
  // Get the selector from the bytecodes.
  __ ldr(R7, Address(R5, 1));

  // Fetch the virtual table from the program.
  __ ldr(R1, Address(R4, Process::ProgramOffset()));
  __ ldr(R1, Address(R1, Program::VTableOffset()));

  if (!test) {
    // Compute the arity from the selector.
    ASSERT(Selector::ArityField::shift() == 0);
    __ and_(R2, R7, Immediate(Selector::ArityField::mask()));
  }

  // Compute the selector offset (smi tagged) from the selector.
  __ ldr(R9, Immediate(Selector::IdField::mask()));
  __ and_(R7, R7, R9);
  __ lsr(R7, R7, Immediate(Selector::IdField::shift() - Smi::kTagSize));

  // Get the receiver from the stack.
  if (test) {
    LoadLocal(R2, 0);
  } else {
    __ neg(R2, R2);
    __ ldr(R2, Address(R6, Operand(R2, TIMES_4)));
  }

  // Compute the receiver class.
  Label smi, dispatch;
  ASSERT(Smi::kTag == 0);
  __ tst(R2, Immediate(Smi::kTagMask));
  __ b(EQ, &smi);
  __ ldr(R2, Address(R2, HeapObject::kClassOffset - HeapObject::kTag));

  // Compute entry index: class id + selector offset.
  int id_offset = Class::kIdOrTransformationTargetOffset - HeapObject::kTag;
  __ Bind(&dispatch);
  __ ldr(R2, Address(R2, id_offset));
  __ add(R2, R2, R7);

  // Fetch the entry from the table. Because the index is smi tagged
  // we only multiply by two -- not four -- when indexing.
  ASSERT(Smi::kTagSize == 1);
  __ add(R1, R1, Immediate(Array::kSize - HeapObject::kTag));
  __ ldr(R1, Address(R1, Operand(R2, TIMES_2)));

  // Validate that the offset stored in the entry matches the offset
  // we used to find it.
  Label invalid;
  __ ldr(R3, Address(R1, Array::kSize - HeapObject::kTag));
  __ cmp(R7, R3);
  __ b(NE, &invalid);

  // Load the target and the intrinsic from the entry.
  Label validated, intrinsified;
  if (test) {
    // Valid entry: The answer is true.
    StoreLocal(R10, 0);
    Dispatch(kInvokeTestLength);
  } else {
    __ Bind(&validated);
    __ ldr(R0, Address(R1, 8 + Array::kSize - HeapObject::kTag));
    __ ldr(R2, Address(R1, 12 + Array::kSize - HeapObject::kTag));

    // Check if we have an associated intrinsic.
    __ tst(R2, R2);
    __ b(NE, &intrinsified);

    // Compute and push the return address on the stack.
    __ add(R5, R5, Immediate(kInvokeMethodVtableLength));
    Push(R5);

    // Jump to the first bytecode in the target method.
    __ add(R5, R0, Immediate(Function::kSize - HeapObject::kTag));
    CheckStackOverflow(0);
    Dispatch(0);
  }

  __ Bind(&smi);
  __ ldr(R2, Address(R4, Process::ProgramOffset()));
  __ ldr(R2, Address(R2, Program::smi_class_offset()));
  __ b(&dispatch);

  if (test) {
    // Invalid entry: The answer is false.
    __ Bind(&invalid);
    StoreLocal(R11, 0);
    Dispatch(kInvokeTestLength);
  } else {
    __ Bind(&intrinsified);
    __ mov(PC, R2);

    // Invalid entry: Use the noSuchMethod entry from entry zero of
    // the virtual table.
    __ Bind(&invalid);
    __ ldr(R1, Address(R4, Process::ProgramOffset()));
    __ ldr(R1, Address(R1, Program::VTableOffset()));
    __ ldr(R1, Address(R1, Array::kSize - HeapObject::kTag));
    __ b(&validated);
  }
}

void InterpreterGeneratorARM::InvokeNative(bool yield) {
  __ ldrb(R1, Address(R5, 1));
  __ neg(R1, R1);
  __ ldrb(R0, Address(R5, 2));

  // Load native from native table.
  __ ldr(R9, "kNativeTable");
  __ ldr(R2, Address(R9, Operand(R0, TIMES_4)));

  // Setup argument (process and pointer to first argument).
  __ add(R7, R6, Operand(R1, TIMES_4));
  __ mov(R1, R7);
  __ mov(R0, R4);

  Label failure;
  __ blx(R2);
  __ and_(R1, R0, Immediate(Failure::kTagMask));
  __ cmp(R1, Immediate(Failure::kTag));
  __ b(EQ, &failure);

  // Result is in r0. Pointer to first argument is in r7. Load return address.
  LoadLocal(R5, 0);

  if (yield) {
    // Set the result to null and drop the arguments.
    __ str(R8, Address(R7, 0));
    __ mov(R6, R7);

    // If the result of calling the native is null, we don't yield.
    Label dont_yield;
    __ cmp(R0, R8);
    __ b(EQ, &dont_yield);

    // Yield to the target port.
    __ ldr(R3, Address(SP, 0));
    __ str(R0, Address(R3, 0));
    __ mov(R0, Immediate(Interpreter::kTargetYield));
    __ b(&done_);
    __ Bind(&dont_yield);
  } else {
    // Store the result in the stack and drop the arguments.
    __ str(R0, Address(R7, 0));
    __ mov(R6, R7);
  }

  // Dispatch to return address.
  Dispatch(0);

  // Failure: Check if it's a request to garbage collect. If not,
  // just continue running the failure block by dispatching to the
  // next bytecode.
  __ Bind(&failure);
  __ cmp(R0, Immediate(reinterpret_cast<int32>(Failure::retry_after_gc())));
  __ b(EQ, &gc_);

  // TODO(kasperl): This should be reworked. We shouldn't be calling
  // through the runtime system for something as simple as converting
  // a failure object to the corresponding heap object.
  __ mov(R1, R0);
  __ mov(R0, R4);
  __ bl("HandleObjectFromFailure");

  Push(R0);
  Dispatch(kInvokeNativeLength);
}

void InterpreterGeneratorARM::InvokeStatic(bool unfolded) {
  if (unfolded) {
    __ ldr(R1, Address(R5, 1));
    __ ldr(R0, Address(R5, Operand(R1, TIMES_1)));
  } else {
    __ ldr(R1, Address(R5, 1));
    __ ldr(R2, Address(R4, Process::ProgramOffset()));
    __ ldr(R3, Address(R2, Program::StaticMethodsOffset()));
    __ add(R3, R3, Immediate(Array::kSize - HeapObject::kTag));
    __ ldr(R0, Address(R3, Operand(R1, TIMES_4)));
  }

  // Compute and push the return address on the stack.
  __ add(R1, R5, Immediate(kInvokeStaticLength));
  Push(R1);

  // Jump to the first bytecode in the target method.
  __ add(R5, R0, Immediate(Function::kSize - HeapObject::kTag));
  CheckStackOverflow(0);
  Dispatch(0);
}

void InterpreterGeneratorARM::Allocate(bool unfolded, bool immutable) {
  // Load the class into register r7.
  if (unfolded) {
    __ ldr(R0, Address(R5, 1));
    __ ldr(R7, Address(R5, Operand(R0, TIMES_1)));
  } else {
    __ ldr(R0, Address(R5, 1));
    __ ldr(R1, Address(R4, Process::ProgramOffset()));
    __ ldr(R1, Address(R1, Program::ClassesOffset()));
    __ add(R1, R1, Immediate(Array::kSize - HeapObject::kTag));
    __ ldr(R7, Address(R1, Operand(R0, TIMES_4)));
  }

  const Register kRegisterAllocateImmutable = R9;
  const Register kRegisterImmutableMembers = R12;

  // We initialize the 3rd argument to "HandleAllocate" to 0, meaning the object
  // we're allocating will not be initialized with pointers to immutable space.
  __ ldr(kRegisterImmutableMembers, Immediate(0));

  // Loop over all arguments and find out if
  //   * all of them are immutable
  //   * there is at least one immutable member
  Label allocate;
  {
    // Initialization of [kRegisterAllocateImmutable] depended on [immutable]
    __ ldr(kRegisterAllocateImmutable,
           Immediate(immutable ? 1 : 0));

    __ ldr(R2, Address(R7, Class::kInstanceFormatOffset - HeapObject::kTag));
    __ ldr(R3, Immediate(InstanceFormat::FixedSizeField::mask()));
    __ and_(R2, R2, R3);
    int size_shift = InstanceFormat::FixedSizeField::shift() - kPointerSizeLog2;
    __ asr(R2, R2, Immediate(size_shift));

    // R2 = SizeOfEntireObject - Instance::kSize
    __ sub(R2, R2, Immediate(Instance::kSize));

    // R3 = StackPointer(R6) - NumberOfFields*kPointerSize
    __ sub(R3, R6, R2);

    Label loop;
    Label loop_with_immutable_field;
    Label loop_with_mutable_field;

    // Increment pointer to point to next field.
    __ Bind(&loop);
    __ add(R3, R3, Immediate(kPointerSize));

    // Test whether R3 > R6. If so we're done and it's immutable.
    __ cmp(R3, R6);
    __ b(HI, &allocate);

    // If Smi, continue the loop.
    __ ldr(R2, Address(R3, 0));
    __ tst(R2, Immediate(Smi::kTagMask));
    __ b(EQ, &loop);

    // Load class of object we want to test immutability of.
    __ ldr(R0, Address(R2, HeapObject::kClassOffset - HeapObject::kTag));

    // Load instance format & handle the three cases:
    //  - boxed => not immutable
    //  - array => not immutable
    //  - Instance => check runtime-tracked bit
    //  - otherwise => immutable

    // NOTE: Our goal is to test whether a value is a [Boxed], an [Array] or an
    // [Instance]. We cannot just compare the [InstanceFormat], since
    // it contains varying information (e.g. size of the instance).
    // But we can take the non-varying parts of the [InstanceFormat], namely the
    // [TypeField].
    uword mask = InstanceFormat::TypeField::mask();
    uword instance_mask = InstanceFormat::instance_format(0).as_uword() & mask;
    uword boxed_mask = InstanceFormat::boxed_format().as_uword() & mask;
    uword array_mask = InstanceFormat::array_format().as_uword() & mask;

    __ ldr(R0, Address(R0, Class::kInstanceFormatOffset - HeapObject::kTag));
    __ ldr(R1, Immediate(mask));
    __ and_(R0, R0, R1);

    // If this is a Boxed, we bail out.
    __ cmp(R0, Immediate(boxed_mask));
    __ b(EQ, &loop_with_mutable_field);

    // If this is an Array, we bail out.
    __ cmp(R0, Immediate(array_mask));
    __ b(EQ, &loop_with_mutable_field);

    // If this is not an Instance, we consider it immutable.
    __ ldr(R1, Immediate(instance_mask));
    __ cmp(R0, R1);
    __ b(NE, &loop_with_immutable_field);

    // Else, we must have an Instance and check the runtime-tracked
    // immutable bit.
    uword im_mask = Instance::FlagsImmutabilityField::encode(true);
    __ ldr(R2, Address(R2, Instance::kFlagsOffset - HeapObject::kTag));
    __ and_(R2, R2, Immediate(im_mask));
    __ cmp(R2, Immediate(im_mask));
    __ b(EQ, &loop_with_immutable_field);

    __ b(&loop_with_mutable_field);

    __ Bind(&loop_with_immutable_field);
    __ ldr(kRegisterImmutableMembers, Immediate(1));
    __ b(&loop);

    __ Bind(&loop_with_mutable_field);
    __ ldr(kRegisterAllocateImmutable, Immediate(0));
    __ b(&loop);
  }

  // TODO(kasperl): Consider inlining this in the interpreter.
  __ Bind(&allocate);
  __ mov(R0, R4);
  __ mov(R1, R7);
  __ mov(R2, kRegisterAllocateImmutable);
  __ mov(R3, kRegisterImmutableMembers);
  __ bl("HandleAllocate");
  __ cmp(R0, Immediate(reinterpret_cast<int32>(Failure::retry_after_gc())));
  __ b(EQ, &gc_);

  __ ldr(R2, Address(R7, Class::kInstanceFormatOffset - HeapObject::kTag));
  __ ldr(R3, Immediate(InstanceFormat::FixedSizeField::mask()));
  __ and_(R2, R2, R3);
  // The fixed size is recorded as the number of pointers. Therefore, the
  // size in bytes is the recorded size multiplied by kPointerSize. Instead
  // of doing the multiplication we shift right by kPointerSizeLog2 less.
  ASSERT(InstanceFormat::FixedSizeField::shift() >= kPointerSizeLog2);
  int size_shift = InstanceFormat::FixedSizeField::shift() - kPointerSizeLog2;
  __ lsr(R2, R2, Immediate(size_shift));

  // Compute the address of the first and last instance field.
  __ sub(R7, R0, Immediate(kWordSize + HeapObject::kTag));
  __ add(R7, R7, R2);
  __ add(R9, R0, Immediate(Instance::kSize - HeapObject::kTag));

  Label loop, done;
  __ Bind(&loop);
  __ cmp(R9, R7);
  __ b(HI, &done);
  Pop(R1);
  __ str(R1, Address(R7, 0));
  __ sub(R7, R7, Immediate(1 * kWordSize));
  __ b(&loop);

  __ Bind(&done);
  Push(R0);
  Dispatch(kAllocateLength);
}


void InterpreterGeneratorARM::AddToStoreBufferSlow(Register object,
                                                   Register value) {
  if (object != R1) {
    ASSERT(value != R1);
    __ mov(R1, object);
  }
  if (value != R2) {
    __ mov(R2, value);
  }
  __ mov(R0, R4);
  __ bl("AddToStoreBufferSlow");
}

void InterpreterGeneratorARM::InvokeCompare(const char* fallback,
                                            Condition cond) {
  LoadLocal(R0, 0);
  __ tst(R0, Immediate(Smi::kTagMask));
  __ b(NE, fallback);
  LoadLocal(R1, 1);
  __ tst(R1, Immediate(Smi::kTagMask));
  __ b(NE, fallback);

  Label true_case;
  __ cmp(R1, R0);
  __ b(cond, &true_case);

  StoreLocal(R11, 1);
  Drop(1);
  Dispatch(5);

  __ Bind(&true_case);
  StoreLocal(R10, 1);
  Drop(1);
  Dispatch(5);
}

void InterpreterGeneratorARM::CheckStackOverflow(int size) {
  __ ldr(R1, Address(R4, Process::StackLimitOffset()));
  __ cmp(R1, R6);
  if (size == 0) {
    __ b(LS, &check_stack_overflow_0_);
  } else {
    Label done;
    __ b(HI, &done);
    __ mov(R0, Immediate(size));
    __ b(&check_stack_overflow_);
    __ Bind(&done);
  }
}

void InterpreterGeneratorARM::Dispatch(int size) {
  // Load the next bytecode through R5 and dispatch to it.
  __ ldrb(R7, Address(R5, size));
  if (size > 0) {
    __ add(R5, R5, Immediate(size));
  }
  __ ldr(R9, "InterpretFast_DispatchTable");
  __ ldr(PC, Address(R9, Operand(R7, TIMES_4)));
  __ GenerateConstantPool();
}

void InterpreterGeneratorARM::SaveState() {
  // Push the bytecode pointer on the stack.
  Push(R5);

  // Update top in the stack. Ugh. Complicated.
  __ ldr(R5, Address(R4, Process::CoroutineOffset()));
  __ ldr(R5, Address(R5, Coroutine::kStackOffset - HeapObject::kTag));
  __ sub(R6, R6, R5);
  __ sub(R6, R6, Immediate(Stack::kSize - HeapObject::kTag));
  __ lsr(R6, R6, Immediate(1));
  __ str(R6, Address(R5, Stack::kTopOffset - HeapObject::kTag));
}

void InterpreterGeneratorARM::RestoreState() {
  // Load the current stack pointer into R6.
  __ ldr(R6, Address(R4, Process::CoroutineOffset()));
  __ ldr(R6, Address(R6, Coroutine::kStackOffset - HeapObject::kTag));
  __ ldr(R5, Address(R6, Stack::kTopOffset - HeapObject::kTag));
  __ add(R6, R6, Immediate(Stack::kSize - HeapObject::kTag));
  __ add(R6, R6, Operand(R5, TIMES_2));

  // Load constants into registers.
  __ ldr(R10, Address(R4, Process::ProgramOffset()));
  __ ldr(R11, Address(R10, Program::false_object_offset()));
  __ ldr(R8, Address(R10, Program::null_object_offset()));
  __ ldr(R10, Address(R10, Program::true_object_offset()));

  // Pop current bytecode pointer from the stack.
  Pop(R5);
}

}  // namespace fletch

#endif  // defined FLETCH_TARGET_ARM
