// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#if defined(FLETCH_TARGET_IA32)

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

class InterpreterGeneratorX86: public InterpreterGenerator {
 public:
  explicit InterpreterGeneratorX86(Assembler* assembler)
      : InterpreterGenerator(assembler) { }

  // Registers
  // ---------
  //   edi: stack pointer (top)
  //   esi: bytecode pointer
  //   ebp: current process
  //

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

  virtual void DoInvokeSelector();

  virtual void DoInvokeTest();
  virtual void DoInvokeTestFast();
  virtual void DoInvokeTestVtable();

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
  // Expects to be called after SaveState with the exception object in EBX.
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
  Label gc_;
  Label check_stack_overflow_;
  Label check_stack_overflow_0_;
  Label intrinsic_failure_;

  void LoadLocal(Register reg, int index);
  void StoreLocal(Register reg, int index);

  void Push(Register reg);
  void Pop(Register reg);
  void Drop(int n);

  void Return(bool wide);

  void Allocate(bool unfolded, bool immutable);

  // This function
  //   * changes the first three stack slots
  //   * changes caller-saved registers
  void AddToStoreBufferSlow(Register object, Register value);

  void InvokeMethod(bool test);
  void InvokeMethodFast(bool test);
  void InvokeMethodVtable(bool test);

  void InvokeStatic(bool unfolded);

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
  void InvokeDivision(const char* fallback, bool quotient);

  void InvokeBitNot(const char* fallback);
  void InvokeBitAnd(const char* fallback);
  void InvokeBitOr(const char* fallback);
  void InvokeBitXor(const char* fallback);
  void InvokeBitShr(const char* fallback);
  void InvokeBitShl(const char* fallback);

  void InvokeNative(bool yield);

  void CheckStackOverflow(int size);

  void Dispatch(int size);

  void SaveState();
  void RestoreState();

  static int ComputeStackPadding(int reserved, int extra) {
    const int kAlignment = 16;
    int rounded = (reserved + extra + kAlignment - 1) & ~(kAlignment - 1);
    return rounded - reserved;
  }
};

GENERATE(, InterpretFast) {
  InterpreterGeneratorX86 generator(assembler);
  generator.Generate();
}

void InterpreterGeneratorX86::GeneratePrologue() {
  __ pushl(EBP);
  __ pushl(EBX);
  __ pushl(EDI);
  __ pushl(ESI);

  // Load the current process into register ebp.
  __ movl(EBP, Address(ESP, (4 + 1) * kWordSize));

  // Pad the stack to guarantee the right alignment for calls.
  int padding = ComputeStackPadding(5 * kWordSize, 4 * kWordSize);
  if (padding > 0) __ subl(ESP, Immediate(padding));

  // Restore the register state and dispatch to the first bytecode.
  RestoreState();
  Dispatch(0);
}

void InterpreterGeneratorX86::GenerateEpilogue() {
  // Done. Start by saving the register state.
  __ Bind(&done_);
  SaveState();

  // Undo stack padding.
  Label undo_padding;
  __ Bind(&undo_padding);
  int padding = ComputeStackPadding(5 * kWordSize, 4 * kWordSize);
  if (padding > 0) __ addl(ESP, Immediate(padding));

  // Restore callee-saved registers.
  __ popl(ESI);
  __ popl(EDI);
  __ popl(EBX);
  __ popl(EBP);
  __ ret();

  // Handle immutable heap allocation failures.
  Label immutable_alloc_failure;
  __ Bind(&immutable_alloc_failure);
  __ movl(EAX, Immediate(Interpreter::kImmutableAllocationFailure));
  __ jmp(&undo_padding);

  // Handle GC and re-interpret current bytecode.
  __ Bind(&gc_);
  SaveState();
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ call("HandleGC");
  __ testl(EAX, EAX);
  __ j(NOT_ZERO, &immutable_alloc_failure);
  RestoreState();
  Dispatch(0);

  // Stack overflow handling (slow case).
  Label stay_fast, overflow;
  __ Bind(&check_stack_overflow_0_);
  __ xorl(EAX, EAX);
  __ Bind(&check_stack_overflow_);
  SaveState();

  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), EAX);
  __ call("HandleStackOverflow");
  __ testl(EAX, EAX);
  ASSERT(Process::kStackCheckContinue == 0);
  __ j(ZERO, &stay_fast);
  __ cmpl(EAX, Immediate(Process::kStackCheckInterrupt));
  __ j(NOT_EQUAL, &overflow);
  __ movl(EAX, Immediate(Interpreter::kInterrupt));
  __ jmp(&undo_padding);

  __ Bind(&stay_fast);
  RestoreState();
  Dispatch(0);

  __ Bind(&overflow);
  __ movl(EBX, Address(EBP, Process::ProgramOffset()));
  __ movl(EBX, Address(EBX, Program::raw_stack_overflow_offset()));
  DoThrowAfterSaveState();

  // Intrinsic failure: Just invoke the method.
  __ Bind(&intrinsic_failure_);
  __ addl(ESI, Immediate(kInvokeMethodLength));
  Push(ESI);
  __ leal(ESI, Address(EAX, Function::kSize - HeapObject::kTag));
  Dispatch(0);
}

void InterpreterGeneratorX86::DoLoadLocal0() {
  LoadLocal(EAX, 0);
  Push(EAX);
  Dispatch(1);
}

void InterpreterGeneratorX86::DoLoadLocal1() {
  LoadLocal(EAX, 1);
  Push(EAX);
  Dispatch(1);
}

void InterpreterGeneratorX86::DoLoadLocal2() {
  LoadLocal(EAX, 2);
  Push(EAX);
  Dispatch(1);
}

void InterpreterGeneratorX86::DoLoadLocal() {
  __ movzbl(EAX, Address(ESI, 1));
  __ negl(EAX);
  __ movl(EAX, Address(EDI, EAX, TIMES_4));
  Push(EAX);
  Dispatch(kLoadLocalLength);
}

void InterpreterGeneratorX86::DoLoadLocalWide() {
  __ movl(EAX, Address(ESI, 1));
  __ negl(EAX);
  __ movl(EAX, Address(EDI, EAX, TIMES_4));
  Push(EAX);
  Dispatch(kLoadLocalWideLength);
}

void InterpreterGeneratorX86::DoLoadBoxed() {
  __ movzbl(EAX, Address(ESI, 1));
  __ negl(EAX);
  __ movl(EBX, Address(EDI, EAX, TIMES_4));
  __ movl(EAX, Address(EBX, Boxed::kValueOffset - HeapObject::kTag));
  Push(EAX);
  Dispatch(kLoadBoxedLength);
}

void InterpreterGeneratorX86::DoLoadStatic() {
  __ movl(EAX, Address(ESI, 1));
  __ movl(EBX, Address(EBP, Process::StaticsOffset()));
  __ movl(EAX, Address(EBX, EAX, TIMES_4, Array::kSize - HeapObject::kTag));
  Push(EAX);
  Dispatch(kLoadStaticLength);
}

void InterpreterGeneratorX86::DoLoadStaticInit() {
  __ movl(EAX, Address(ESI, 1));
  __ movl(EBX, Address(EBP, Process::StaticsOffset()));
  __ movl(EAX, Address(EBX, EAX, TIMES_4, Array::kSize - HeapObject::kTag));

  Label done;
  ASSERT(Smi::kTag == 0);
  __ testl(EAX, Immediate(Smi::kTagMask));
  __ j(ZERO, &done);
  __ movl(EBX, Address(EAX, HeapObject::kClassOffset - HeapObject::kTag));
  __ movl(EBX, Address(EBX, Class::kInstanceFormatOffset - HeapObject::kTag));

  int type = InstanceFormat::INITIALIZER_TYPE;
  __ andl(EBX, Immediate(InstanceFormat::TypeField::mask()));
  __ cmpl(EBX, Immediate(type << InstanceFormat::TypeField::shift()));
  __ j(NOT_EQUAL, &done);

  // Invoke the initializer function.
  __ movl(EAX, Address(EAX, Initializer::kFunctionOffset - HeapObject::kTag));
  __ addl(ESI, Immediate(kInvokeMethodLength));
  Push(ESI);

  // Jump to the first bytecode in the initializer function.
  __ leal(ESI, Address(EAX, Function::kSize - HeapObject::kTag));
  CheckStackOverflow(0);
  Dispatch(0);

  __ Bind(&done);
  Push(EAX);
  Dispatch(kLoadStaticInitLength);
}

void InterpreterGeneratorX86::DoLoadField() {
  __ movzbl(EBX, Address(ESI, 1));
  LoadLocal(EAX, 0);
  __ movl(EAX, Address(EAX, EBX, TIMES_4, Instance::kSize - HeapObject::kTag));
  StoreLocal(EAX, 0);
  Dispatch(kLoadFieldLength);
}

void InterpreterGeneratorX86::DoLoadFieldWide() {
  __ movl(EBX, Address(ESI, 1));
  LoadLocal(EAX, 0);
  __ movl(EAX, Address(EAX, EBX, TIMES_4, Instance::kSize - HeapObject::kTag));
  StoreLocal(EAX, 0);
  Dispatch(kLoadFieldWideLength);
}

void InterpreterGeneratorX86::DoLoadConst() {
  __ movl(EAX, Address(ESI, 1));
  __ movl(EBX, Address(EBP, Process::ProgramOffset()));
  __ movl(EBX, Address(EBX, Program::ConstantsOffset()));
  __ movl(EAX, Address(EBX, EAX, TIMES_4, Array::kSize - HeapObject::kTag));
  Push(EAX);
  Dispatch(kLoadConstLength);
}

void InterpreterGeneratorX86::DoLoadConstUnfold() {
  __ movl(EAX, Address(ESI, 1));
  __ movl(EAX, Address(ESI, EAX, TIMES_1));
  Push(EAX);
  Dispatch(kLoadConstUnfoldLength);
}

void InterpreterGeneratorX86::DoStoreLocal() {
  LoadLocal(EBX, 0);
  __ movzbl(EAX, Address(ESI, 1));
  __ negl(EAX);
  __ movl(Address(EDI, EAX, TIMES_4), EBX);
  Dispatch(2);
}

void InterpreterGeneratorX86::DoStoreBoxed() {
  LoadLocal(ECX, 0);
  __ movzbl(EAX, Address(ESI, 1));
  __ negl(EAX);
  __ movl(EBX, Address(EDI, EAX, TIMES_4));
  __ movl(Address(EBX, Boxed::kValueOffset - HeapObject::kTag), ECX);

  AddToStoreBufferSlow(EBX, ECX);

  Dispatch(kStoreBoxedLength);
}

void InterpreterGeneratorX86::DoStoreStatic() {
  LoadLocal(ECX, 0);
  __ movl(EAX, Address(ESI, 1));
  __ movl(EBX, Address(EBP, Process::StaticsOffset()));
  __ movl(Address(EBX, EAX, TIMES_4, Array::kSize - HeapObject::kTag), ECX);

  AddToStoreBufferSlow(EBX, ECX);

  Dispatch(kStoreStaticLength);
}

void InterpreterGeneratorX86::DoStoreField() {
  __ movzbl(EBX, Address(ESI, 1));
  LoadLocal(ECX, 0);
  LoadLocal(EAX, 1);
  __ movl(Address(EAX, EBX, TIMES_4, Instance::kSize - HeapObject::kTag), ECX);
  StoreLocal(ECX, 1);
  Drop(1);

  AddToStoreBufferSlow(EAX, ECX);

  Dispatch(kStoreFieldLength);
}

void InterpreterGeneratorX86::DoStoreFieldWide() {
  __ movl(EBX, Address(ESI, 1));
  LoadLocal(ECX, 0);
  LoadLocal(EAX, 1);
  __ movl(Address(EAX, EBX, TIMES_4, Instance::kSize - HeapObject::kTag), ECX);
  StoreLocal(ECX, 1);
  Drop(1);

  AddToStoreBufferSlow(EAX, ECX);

  Dispatch(kStoreFieldWideLength);
}

void InterpreterGeneratorX86::DoLoadLiteralNull() {
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::null_object_offset()));
  Push(EAX);
  Dispatch(1);
}

void InterpreterGeneratorX86::DoLoadLiteralTrue() {
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::true_object_offset()));
  Push(EAX);
  Dispatch(1);
}

void InterpreterGeneratorX86::DoLoadLiteralFalse() {
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::false_object_offset()));
  Push(EAX);
  Dispatch(1);
}

void InterpreterGeneratorX86::DoLoadLiteral0() {
  __ movl(EAX, Immediate(reinterpret_cast<int32>(Smi::FromWord(0))));
  Push(EAX);
  Dispatch(1);
}

void InterpreterGeneratorX86::DoLoadLiteral1() {
  __ movl(EAX, Immediate(reinterpret_cast<int32>(Smi::FromWord(1))));
  Push(EAX);
  Dispatch(1);
}

void InterpreterGeneratorX86::DoLoadLiteral() {
  __ movzbl(EAX, Address(ESI, 1));
  __ shll(EAX, Immediate(Smi::kTagSize));
  ASSERT(Smi::kTag == 0);
  Push(EAX);
  Dispatch(2);
}

void InterpreterGeneratorX86::DoLoadLiteralWide() {
  ASSERT(Smi::kTag == 0);
  __ movl(EAX, Address(ESI, 1));
  __ shll(EAX, Immediate(Smi::kTagSize));
  Push(EAX);
  Dispatch(kLoadLiteralWideLength);
}

void InterpreterGeneratorX86::DoInvokeMethod() {
  InvokeMethod(false);
}

void InterpreterGeneratorX86::DoInvokeMethodFast() {
  InvokeMethodFast(false);
}

void InterpreterGeneratorX86::DoInvokeMethodVtable() {
  InvokeMethodVtable(false);
}

void InterpreterGeneratorX86::DoInvokeTest() {
  InvokeMethod(true);
}

void InterpreterGeneratorX86::DoInvokeTestFast() {
  InvokeMethodFast(true);
}

void InterpreterGeneratorX86::DoInvokeTestVtable() {
  InvokeMethodVtable(true);
}

void InterpreterGeneratorX86::DoInvokeStatic() {
  InvokeStatic(false);
}

void InterpreterGeneratorX86::DoInvokeStaticUnfold() {
  InvokeStatic(true);
}

void InterpreterGeneratorX86::DoInvokeFactory() {
  InvokeStatic(false);
}

void InterpreterGeneratorX86::DoInvokeFactoryUnfold() {
  InvokeStatic(true);
}

void InterpreterGeneratorX86::DoInvokeNative() {
  InvokeNative(false);
}

void InterpreterGeneratorX86::DoInvokeNativeYield() {
  InvokeNative(true);
}

void InterpreterGeneratorX86::DoInvokeSelector() {
  SaveState();
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ call("HandleInvokeSelector");
  RestoreState();
  CheckStackOverflow(0);
  Dispatch(0);
}

void InterpreterGeneratorX86::InvokeEq(const char* fallback) {
  InvokeCompare(fallback, EQUAL);
}

void InterpreterGeneratorX86::InvokeLt(const char* fallback) {
  InvokeCompare(fallback, LESS);
}

void InterpreterGeneratorX86::InvokeLe(const char* fallback) {
  InvokeCompare(fallback, LESS_EQUAL);
}

void InterpreterGeneratorX86::InvokeGt(const char* fallback) {
  InvokeCompare(fallback, GREATER);
}

void InterpreterGeneratorX86::InvokeGe(const char* fallback) {
  InvokeCompare(fallback, GREATER_EQUAL);
}

void InterpreterGeneratorX86::InvokeAdd(const char* fallback) {
  LoadLocal(EAX, 1);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(EBX, 0);
  __ testl(EBX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  __ addl(EAX, EBX);
  __ j(OVERFLOW_, fallback);
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeAddLength);
}

void InterpreterGeneratorX86::InvokeSub(const char* fallback) {
  LoadLocal(EAX, 1);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(EBX, 0);
  __ testl(EBX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  __ subl(EAX, EBX);
  __ j(OVERFLOW_, fallback);
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeSubLength);
}

void InterpreterGeneratorX86::InvokeMod(const char* fallback) {
  // TODO(ajohnsen): idiv may yield a negative remainder.
  __ jmp(fallback);
}

void InterpreterGeneratorX86::InvokeMul(const char* fallback) {
  LoadLocal(EAX, 1);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(EBX, 0);
  __ testl(EBX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  // Untag and multiply.
  __ sarl(EAX, Immediate(1));
  __ sarl(EBX, Immediate(1));
  __ imul(EBX);
  __ j(OVERFLOW_, fallback);

  // Re-tag. We need to check for overflow to handle the case
  // where the top two bits are 01 after the multiplication.
  ASSERT(Smi::kTagSize == 1 && Smi::kTag == 0);
  __ addl(EAX, EAX);
  __ j(OVERFLOW_, fallback);

  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeMulLength);
}

void InterpreterGeneratorX86::InvokeTruncDiv(const char* fallback) {
  InvokeDivision(fallback, true);
}

void InterpreterGeneratorX86::InvokeBitNot(const char* fallback) {
  LoadLocal(EAX, 0);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  __ notl(EAX);
  __ andl(EAX, Immediate(~Smi::kTagMask));
  StoreLocal(EAX, 0);
  Dispatch(kInvokeBitNotLength);
}

void InterpreterGeneratorX86::InvokeBitAnd(const char* fallback) {
  LoadLocal(EAX, 1);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(EBX, 0);
  __ testl(EBX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  __ andl(EAX, EBX);
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeBitAndLength);
}

void InterpreterGeneratorX86::InvokeBitOr(const char* fallback) {
  LoadLocal(EAX, 1);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(EBX, 0);
  __ testl(EBX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  __ orl(EAX, EBX);
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeBitAndLength);
}

void InterpreterGeneratorX86::InvokeBitXor(const char* fallback) {
  LoadLocal(EAX, 1);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(EBX, 0);
  __ testl(EBX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  __ xorl(EAX, EBX);
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeBitAndLength);
}

void InterpreterGeneratorX86::InvokeBitShr(const char* fallback) {
  LoadLocal(EAX, 1);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(ECX, 0);
  __ testl(ECX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  // Untag the smis and do the shift.
  __ sarl(EAX, Immediate(1));
  __ sarl(ECX, Immediate(1));
  __ sarl_cl(EAX);

  // Re-tag the resulting smi. No need to check for overflow
  // here, because the top two bits of eax are either 00 or 11
  // because we've shifted eax arithmetically at least one
  // position to the right.
  ASSERT(Smi::kTagSize == 1 && Smi::kTag == 0);
  __ addl(EAX, EAX);

  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeBitAndLength);
}

void InterpreterGeneratorX86::InvokeBitShl(const char* fallback) {
  LoadLocal(EAX, 1);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(ECX, 0);
  __ testl(ECX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  // Untag the shift count, but not the value. If the shift
  // count is greater than 31 (or negative), the shift is going
  // to misbehave so we have to guard against that.
  __ sarl(ECX, Immediate(1));
  __ cmpl(ECX, Immediate(32));
  __ j(ABOVE_EQUAL, fallback);

  // Only allow to shift out "sign bits". If we shift
  // out any other bit, it's an overflow.
  __ movl(EBX, EAX);
  __ shll_cl(EAX);
  __ movl(EDX, EAX);
  __ sarl_cl(EDX);
  __ cmpl(EBX, EDX);
  __ j(NOT_EQUAL, fallback);

  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeBitShlLength);
}

void InterpreterGeneratorX86::DoPop() {
  Drop(1);
  Dispatch(1);
}

void InterpreterGeneratorX86::DoReturn() {
  Return(false);
}

void InterpreterGeneratorX86::DoReturnWide() {
  Return(true);
}

void InterpreterGeneratorX86::DoBranchWide() {
  __ movl(EAX, Address(ESI, 1));
  __ addl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoBranchIfTrueWide() {
  Label branch;
  Pop(EBX);
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::true_object_offset()));
  __ cmpl(EBX, EAX);
  __ j(EQUAL, &branch);
  Dispatch(kBranchIfTrueWideLength);

  __ Bind(&branch);
  __ movl(EAX, Address(ESI, 1));
  __ addl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoBranchIfFalseWide() {
  Label branch;
  Pop(EBX);
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::true_object_offset()));
  __ cmpl(EBX, EAX);
  __ j(NOT_EQUAL, &branch);
  Dispatch(kBranchIfFalseWideLength);

  __ Bind(&branch);
  __ movl(EAX, Address(ESI, 1));
  __ addl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoBranchBack() {
  CheckStackOverflow(0);
  __ movzbl(EAX, Address(ESI, 1));
  __ subl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoBranchBackIfTrue() {
  CheckStackOverflow(0);

  Label branch;
  Pop(EBX);
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::true_object_offset()));
  __ cmpl(EBX, EAX);
  __ j(EQUAL, &branch);
  Dispatch(kBranchBackIfTrueLength);

  __ Bind(&branch);
  __ movzbl(EAX, Address(ESI, 1));
  __ subl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoBranchBackIfFalse() {
  CheckStackOverflow(0);

  Label branch;
  Pop(EBX);
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::true_object_offset()));
  __ cmpl(EBX, EAX);
  __ j(NOT_EQUAL, &branch);
  Dispatch(kBranchBackIfFalseLength);

  __ Bind(&branch);
  __ movzbl(EAX, Address(ESI, 1));
  __ subl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoBranchBackWide() {
  CheckStackOverflow(0);
  __ movl(EAX, Address(ESI, 1));
  __ subl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoBranchBackIfTrueWide() {
  CheckStackOverflow(0);

  Label branch;
  Pop(EBX);
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::true_object_offset()));
  __ cmpl(EBX, EAX);
  __ j(EQUAL, &branch);
  Dispatch(kBranchBackIfTrueWideLength);

  __ Bind(&branch);
  __ movl(EAX, Address(ESI, 1));
  __ subl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoBranchBackIfFalseWide() {
  CheckStackOverflow(0);

  Label branch;
  Pop(EBX);
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::true_object_offset()));
  __ cmpl(EBX, EAX);
  __ j(NOT_EQUAL, &branch);
  Dispatch(kBranchBackIfFalseWideLength);

  __ Bind(&branch);
  __ movl(EAX, Address(ESI, 1));
  __ subl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoPopAndBranchWide() {
  __ movzbl(EAX, Address(ESI, 1));
  __ negl(EAX);
  __ leal(EDI, Address(EDI, EAX, TIMES_4));

  __ movl(EAX, Address(ESI, 2));
  __ addl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoPopAndBranchBackWide() {
  CheckStackOverflow(0);

  __ movzbl(EAX, Address(ESI, 1));
  __ negl(EAX);
  __ leal(EDI, Address(EDI, EAX, TIMES_4));

  __ movl(EAX, Address(ESI, 2));
  __ subl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoAllocate() {
  Allocate(false, false);
}

void InterpreterGeneratorX86::DoAllocateUnfold() {
  Allocate(true, false);
}

void InterpreterGeneratorX86::DoAllocateImmutable() {
  Allocate(false, true);
}

void InterpreterGeneratorX86::DoAllocateImmutableUnfold() {
  Allocate(true, true);
}

void InterpreterGeneratorX86::DoAllocateBoxed() {
  LoadLocal(EBX, 0);
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), EBX);
  __ call("HandleAllocateBoxed");
  __ cmpl(EAX, Immediate(reinterpret_cast<int32>(Failure::retry_after_gc())));
  __ j(EQUAL, &gc_);
  StoreLocal(EAX, 0);
  Dispatch(kAllocateBoxedLength);
}

void InterpreterGeneratorX86::DoNegate() {
  Label store;
  LoadLocal(EBX, 0);
  __ movl(ECX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(ECX, Program::true_object_offset()));
  __ cmpl(EBX, EAX);
  __ j(NOT_EQUAL, &store);
  __ movl(EAX, Address(ECX, Program::false_object_offset()));
  __ Bind(&store);
  StoreLocal(EAX, 0);
  Dispatch(kNegateLength);
}

void InterpreterGeneratorX86::DoStackOverflowCheck() {
  __ movl(EAX, Address(ESI, 1));
  __ movl(EBX, Address(EBP, Process::StackLimitOffset()));
  __ leal(ECX, Address(EDI, EAX, TIMES_4));
  __ cmpl(ECX, EBX);
  __ j(ABOVE_EQUAL, &check_stack_overflow_);
  Dispatch(kStackOverflowCheckLength);
}

void InterpreterGeneratorX86::DoThrow() {
  LoadLocal(EBX, 0);
  SaveState();
  DoThrowAfterSaveState();
}

void InterpreterGeneratorX86::DoThrowAfterSaveState() {
  // Use the stack to store the stack delta initialized to zero.
  __ leal(EAX, Address(ESP, 3 * kWordSize));
  __ movl(Address(EAX, 0), Immediate(0));

  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), EBX);
  __ movl(Address(ESP, 2 * kWordSize), EAX);
  __ call("HandleThrow");

  RestoreState();

  Label unwind;
  __ testl(EAX, EAX);
  __ j(NOT_ZERO, &unwind);
  __ movl(EAX, Immediate(Interpreter::kUncaughtException));
  __ jmp(&done_);

  __ Bind(&unwind);
  __ movl(ECX, Address(ESP, 3 * kWordSize));
  __ negl(ECX);
  __ movl(ESI, EAX);
  __ leal(EDI, Address(EDI, ECX, TIMES_4, 1 * kWordSize));
  StoreLocal(EBX, 0);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoSubroutineCall() {
  __ movl(EAX, Address(ESI, 1));
  __ movl(EBX, Address(ESI, 5));

  // Push the return delta as a tagged smi.
  ASSERT(Smi::kTag == 0);
  __ shll(EBX, Immediate(Smi::kTagSize));
  Push(EBX);

  __ addl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoSubroutineReturn() {
  Pop(EAX);
  __ shrl(EAX, Immediate(Smi::kTagSize));
  __ subl(ESI, EAX);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoProcessYield() {
  __ movl(ECX, Address(EBP, Process::ProgramOffset()));
  __ movl(EBX, Address(ECX, Program::null_object_offset()));
  LoadLocal(EAX, 0);
  __ sarl(EAX, Immediate(1));
  __ addl(ESI, Immediate(kProcessYieldLength));
  StoreLocal(EBX, 0);
  __ jmp(&done_);
}

void InterpreterGeneratorX86::DoCoroutineChange() {
  __ movl(ECX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(ECX, Program::null_object_offset()));

  LoadLocal(EBX, 0);  // Load argument.
  LoadLocal(EDX, 1);  // Load coroutine.

  StoreLocal(EAX, 0);
  StoreLocal(EAX, 1);

  SaveState();
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), EDX);
  __ call("HandleCoroutineChange");
  RestoreState();

  StoreLocal(EBX, 1);
  Drop(1);

  Dispatch(kCoroutineChangeLength);
}

void InterpreterGeneratorX86::DoIdentical() {
  LoadLocal(EAX, 0);
  LoadLocal(EBX, 1);

  // TODO(ager): For now we bail out if we have two doubles or two
  // large integers and let the slow interpreter deal with it. These
  // cases could be dealt with directly here instead.
  Label fast_case;
  Label bail_out;

  // If either is a smi they are not both doubles or large integers.
  __ testl(EAX, Immediate(Smi::kTagMask));
  __ j(ZERO, &fast_case);
  __ testl(EBX, Immediate(Smi::kTagMask));
  __ j(ZERO, &fast_case);

  // If they do not have the same type they are not both double or
  // large integers.
  __ movl(ECX, Address(EAX, HeapObject::kClassOffset - HeapObject::kTag));
  __ movl(ECX, Address(ECX, Class::kInstanceFormatOffset - HeapObject::kTag));
  __ movl(EDX, Address(EBX, HeapObject::kClassOffset - HeapObject::kTag));
  __ cmpl(ECX, Address(EDX, Class::kInstanceFormatOffset - HeapObject::kTag));
  __ j(NOT_EQUAL, &fast_case);

  int double_type = InstanceFormat::DOUBLE_TYPE;
  int large_integer_type = InstanceFormat::LARGE_INTEGER_TYPE;
  int type_field_shift = InstanceFormat::TypeField::shift();

  __ andl(ECX, Immediate(InstanceFormat::TypeField::mask()));
  __ cmpl(ECX, Immediate(double_type << type_field_shift));
  __ j(EQUAL, &bail_out);
  __ cmpl(ECX, Immediate(large_integer_type << type_field_shift));
  __ j(EQUAL, &bail_out);

  __ Bind(&fast_case);
  __ movl(ECX, Address(EBP, Process::ProgramOffset()));

  Label true_case;
  __ cmpl(EBX, EAX);
  __ j(EQUAL, &true_case);

  __ movl(EAX, Address(ECX, Program::false_object_offset()));
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kIdenticalLength);

  __ Bind(&true_case);
  __ movl(EAX, Address(ECX, Program::true_object_offset()));

  Label done;
  __ Bind(&done);
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kIdenticalLength);

  __ Bind(&bail_out);
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), EBX);
  __ movl(Address(ESP, 2 * kWordSize), EAX);
  __ call("HandleIdentical");
  __ jmp(&done);
}

void InterpreterGeneratorX86::DoIdenticalNonNumeric() {
  LoadLocal(EAX, 0);
  LoadLocal(EBX, 1);
  __ movl(ECX, Address(EBP, Process::ProgramOffset()));

  Label true_case;
  __ cmpl(EAX, EBX);
  __ j(EQUAL, &true_case);

  __ movl(EAX, Address(ECX, Program::false_object_offset()));
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kIdenticalNonNumericLength);

  __ Bind(&true_case);
  __ movl(EAX, Address(ECX, Program::true_object_offset()));
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kIdenticalNonNumericLength);
}

void InterpreterGeneratorX86::DoEnterNoSuchMethod() {
  SaveState();
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ call("HandleEnterNoSuchMethod");
  RestoreState();
  Dispatch(0);
}

void InterpreterGeneratorX86::DoExitNoSuchMethod() {
  Pop(EAX);  // Result.
  Pop(EBX);  // Selector.
  __ shrl(EBX, Immediate(Smi::kTagSize));
  Drop(1);   // Sentinel.
  Pop(ESI);

  Label done;
  __ movl(ECX, EBX);
  __ andl(ECX, Immediate(Selector::KindField::mask()));
  __ cmpl(ECX, Immediate(Selector::SETTER << Selector::KindField::shift()));
  __ j(NOT_EQUAL, &done);
  LoadLocal(EAX, 0);

  __ Bind(&done);
  ASSERT(Selector::ArityField::shift() == 0);
  __ andl(EBX, Immediate(Selector::ArityField::mask()));
  __ negl(EBX);

  // Drop the arguments from the stack, but leave the receiver.
  __ leal(EDI, Address(EDI, EBX, TIMES_4));

  StoreLocal(EAX, 0);
  Dispatch(0);
}

void InterpreterGeneratorX86::DoFrameSize() {
  __ int3();
}

void InterpreterGeneratorX86::DoMethodEnd() {
  __ int3();
}

void InterpreterGeneratorX86::DoIntrinsicObjectEquals() {
  Label true_case;
  LoadLocal(EAX, 0);
  LoadLocal(EBX, 1);
  __ movl(ECX, Address(EBP, Process::ProgramOffset()));

  __ cmpl(EAX, EBX);
  __ j(EQUAL, &true_case);

  __ movl(EAX, Address(ECX, Program::false_object_offset()));
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeMethodLength);

  __ Bind(&true_case);
  __ movl(EAX, Address(ECX, Program::true_object_offset()));
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorX86::DoIntrinsicGetField() {
  __ movzbl(EBX, Address(EAX, 2 + Function::kSize - HeapObject::kTag));
  LoadLocal(EAX, 0);
  __ movl(EAX, Address(EAX, EBX, TIMES_4, Instance::kSize - HeapObject::kTag));
  StoreLocal(EAX, 0);
  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorX86::DoIntrinsicSetField() {
  __ movzbl(EBX, Address(EAX, 3 + Function::kSize - HeapObject::kTag));
  LoadLocal(EAX, 0);
  LoadLocal(ECX, 1);
  __ movl(Address(ECX, EBX, TIMES_4, Instance::kSize - HeapObject::kTag), EAX);
  StoreLocal(EAX, 1);
  Drop(1);

  AddToStoreBufferSlow(ECX, EAX);

  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorX86::DoIntrinsicListIndexGet() {
  LoadLocal(EBX, 0);  // Index.
  LoadLocal(ECX, 1);  // List.

  Label failure;
  ASSERT(Smi::kTag == 0);
  __ testl(EBX, Immediate(Smi::kTagMask));
  __ j(NOT_ZERO, &intrinsic_failure_);
  __ cmpl(EBX, Immediate(0));
  __ j(LESS, &intrinsic_failure_);

  // Load the backing store (array) from the first instance field of the list.
  __ movl(ECX, Address(ECX, Instance::kSize - HeapObject::kTag));
  __ movl(EDX, Address(ECX, Array::kLengthOffset - HeapObject::kTag));

  // Check the index against the length.
  __ cmpl(EBX, EDX);
  __ j(GREATER_EQUAL, &intrinsic_failure_);

  // Load from the array and continue.
  ASSERT(Smi::kTagSize == 1);
  __ movl(EAX, Address(ECX, EBX, TIMES_2, Array::kSize - HeapObject::kTag));
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorX86::DoIntrinsicListIndexSet() {
  LoadLocal(EBX, 1);  // Index.
  LoadLocal(ECX, 2);  // List.

  ASSERT(Smi::kTag == 0);
  __ testl(EBX, Immediate(Smi::kTagMask));
  __ j(NOT_ZERO, &intrinsic_failure_);
  __ cmpl(EBX, Immediate(0));
  __ j(LESS, &intrinsic_failure_);

  // Load the backing store (array) from the first instance field of the list.
  __ movl(ECX, Address(ECX, Instance::kSize - HeapObject::kTag));
  __ movl(EDX, Address(ECX, Array::kLengthOffset - HeapObject::kTag));

  // Check the index against the length.
  __ cmpl(EBX, EDX);
  __ j(GREATER_EQUAL, &intrinsic_failure_);

  // Store to the array and continue.
  ASSERT(Smi::kTagSize == 1);
  LoadLocal(EAX, 0);
  // TODO(kustermann): Why ist this TIMES_2.
  __ movl(Address(ECX, EBX, TIMES_2, Array::kSize - HeapObject::kTag), EAX);
  StoreLocal(EAX, 2);
  Drop(2);

  AddToStoreBufferSlow(ECX, EAX);

  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorX86::DoIntrinsicListLength() {
  // Load the backing store (array) from the first instance field of the list.
  LoadLocal(ECX, 0);  // List.
  __ movl(ECX, Address(ECX, Instance::kSize - HeapObject::kTag));
  __ movl(EDX, Address(ECX, Array::kLengthOffset - HeapObject::kTag));
  StoreLocal(EDX, 0);
  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorX86::Push(Register reg) {
  // By storing before updating register edi we (try) to avoid stalls
  // due to writing indireclty through a just updated register.
  StoreLocal(reg, -1);
  __ addl(EDI, Immediate(1 * kWordSize));
}

void InterpreterGeneratorX86::Pop(Register reg) {
  LoadLocal(reg, 0);
  Drop(1);
}

void InterpreterGeneratorX86::Drop(int n) {
  __ subl(EDI, Immediate(n * kWordSize));
}

void InterpreterGeneratorX86::LoadLocal(Register reg, int index) {
  __ movl(reg, Address(EDI, -index * kWordSize));
}

void InterpreterGeneratorX86::StoreLocal(Register reg, int index) {
  __ movl(Address(EDI, -index * kWordSize), reg);
}

void InterpreterGeneratorX86::Return(bool wide) {
  // Get the result from the stack.
  LoadLocal(EAX, 0);

  // Fetch the number of locals and arguments from the bytecodes.
  // Unfortunately, we have to negate the counts so we can use them
  // to index into the stack (grows towards higher addresses).
  if (wide) {
    __ movl(ECX, Address(ESI, 1));
    __ movzbl(EBX, Address(ESI, 5));
  } else {
    __ movzbl(ECX, Address(ESI, 1));
    __ movzbl(EBX, Address(ESI, 2));
  }
  __ negl(ECX);

  // Load the return address.
  __ movl(ESI, Address(EDI, ECX, TIMES_4));

  // Drop both locals and arguments except one which we will overwrite
  // with the result (we've left the return address on the stack).
  __ subl(ECX, EBX);
  __ leal(EDI, Address(EDI, ECX, TIMES_4));

  // Overwrite the first argument (or the return address) with the result
  // and dispatch to the next bytecode.
  StoreLocal(EAX, 0);
  Dispatch(0);
}

void InterpreterGeneratorX86::Allocate(bool unfolded, bool immutable) {
  // Load the class into register ebx.
  if (unfolded) {
    __ movl(EAX, Address(ESI, 1));
    __ movl(EBX, Address(ESI, EAX, TIMES_1));
  } else {
    __ movl(EAX, Address(ESI, 1));
    __ movl(EBX, Address(EBP, Process::ProgramOffset()));
    __ movl(EBX, Address(EBX, Program::ClassesOffset()));
    __ movl(EBX, Address(EBX, EAX, TIMES_4, Array::kSize - HeapObject::kTag));
  }

  const int kStackAllocateImmutable = 2 * kWordSize;
  const int kStackImmutableMembers = 3 * kWordSize;

  // We initialize the 3rd argument to "HandleAllocate" to 0, meaning the object
  // we're allocating will not be initialized with pointers to immutable space.
  __ movl(Address(ESP, kStackImmutableMembers), Immediate(0));

  // Loop over all arguments and find out if
  //   * all of them are immutable
  //   * there is at least one immutable member
  Label allocate;
  {
    // Initialization of [kStackAllocateImmutable] depended on [immutable]
    __ movl(Address(ESP, kStackAllocateImmutable),
            Immediate(immutable ? 1 : 0));

    __ movl(ECX, Address(EBX, Class::kInstanceFormatOffset - HeapObject::kTag));
    __ andl(ECX, Immediate(InstanceFormat::FixedSizeField::mask()));
    int size_shift = InstanceFormat::FixedSizeField::shift() - kPointerSizeLog2;
    __ shrl(ECX, Immediate(size_shift));

    // ECX = SizeOfEntireObject - Instance::kSize
    __ subl(ECX, Immediate(Instance::kSize));

    // EDX = StackPointer(EDI) - NumberOfFields*kPointerSize
    __ movl(EDX, EDI);
    __ subl(EDX, ECX);

    Label loop;
    Label loop_with_immutable_field;
    Label loop_with_mutable_field;

    // Increment pointer to point to next field.
    __ Bind(&loop);
    __ addl(EDX, Immediate(kPointerSize));

    // Test whether EDX > EDI. If so we're done and it's immutable.
    __ cmpl(EDX, EDI);
    __ j(ABOVE, &allocate);

    // If Smi, continue the loop.
    __ movl(ECX, Address(EDX));
    __ testl(ECX, Immediate(Smi::kTagMask));
    __ j(ZERO, &loop);

    // Load class of object we want to test immutability of.
    __ movl(EAX, Address(ECX, HeapObject::kClassOffset - HeapObject::kTag));

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

    __ movl(EAX, Address(EAX, Class::kInstanceFormatOffset - HeapObject::kTag));
    __ andl(EAX, Immediate(mask));

    // If this is a Boxed, we bail out.
    __ cmpl(EAX, Immediate(boxed_mask));
    __ j(EQUAL, &loop_with_mutable_field);

    // If this is an Array, we bail out.
    __ cmpl(EAX, Immediate(array_mask));
    __ j(EQUAL, &loop_with_mutable_field);

    // If this is not an Instance, we consider it immutable.
    __ cmpl(EAX, Immediate(instance_mask));
    __ j(NOT_EQUAL, &loop_with_immutable_field);

    // Else, we must have a Instance and check the runtime-tracked
    // immutable bit.
    uword im_mask = Instance::FlagsImmutabilityField::encode(true);
    __ movl(ECX, Address(ECX, Instance::kFlagsOffset - HeapObject::kTag));
    __ testl(ECX, Immediate(im_mask));
    __ j(NOT_ZERO, &loop_with_immutable_field);

    __ jmp(&loop_with_mutable_field);

    __ Bind(&loop_with_immutable_field);
    __ movl(Address(ESP, kStackImmutableMembers), Immediate(1));
    __ jmp(&loop);

    __ Bind(&loop_with_mutable_field);
    __ movl(Address(ESP, kStackAllocateImmutable), Immediate(0));
    __ jmp(&loop);
  }

  // TODO(kasperl): Consider inlining this in the interpreter.
  __ Bind(&allocate);
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), EBX);
  // NOTE: The 3nd argument is already pressent ESP + kStackImmutableMembers
  // NOTE: The 4rd argument is already present  ESP + kStackAllocateImmutable
  __ call("HandleAllocate");
  __ cmpl(EAX, Immediate(reinterpret_cast<int32>(Failure::retry_after_gc())));
  __ j(EQUAL, &gc_);

  __ movl(ECX, Address(EBX, Class::kInstanceFormatOffset - HeapObject::kTag));
  __ andl(ECX, Immediate(InstanceFormat::FixedSizeField::mask()));
  // The fixed size is recorded as the number of pointers. Therefore, the
  // size in bytes is the recorded size multiplied by kPointerSize. Instead
  // of doing the multiplication we shift by kPointerSizeLog2 less.
  ASSERT(InstanceFormat::FixedSizeField::shift() >= kPointerSizeLog2);
  int size_shift = InstanceFormat::FixedSizeField::shift() - kPointerSizeLog2;
  __ shrl(ECX, Immediate(size_shift));

  // Compute the address of the first and last instance field.
  __ leal(EDX, Address(EAX, ECX, TIMES_1, -1 * kWordSize - HeapObject::kTag));
  __ leal(ECX, Address(EAX, Instance::kSize - HeapObject::kTag));

  Label loop, done;
  __ Bind(&loop);
  __ cmpl(EDX, ECX);
  __ j(BELOW, &done);
  Pop(EBX);
  __ movl(Address(EDX, 0), EBX);
  __ subl(EDX, Immediate(1 * kWordSize));
  __ jmp(&loop);

  __ Bind(&done);
  Push(EAX);
  Dispatch(kAllocateLength);
}

void InterpreterGeneratorX86::AddToStoreBufferSlow(Register object,
                                                   Register value) {
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), object);
  __ movl(Address(ESP, 2 * kWordSize), value);
  __ call("AddToStoreBufferSlow");
}

void InterpreterGeneratorX86::InvokeMethod(bool test) {
  // Get the selector from the bytecodes.
  __ movl(EDX, Address(ESI, 1));

  if (test) {
    // Get the receiver from the stack.
    LoadLocal(EBX, 0);
  } else {
    // Compute the arity from the selector.
    ASSERT(Selector::ArityField::shift() == 0);
    __ movl(EBX, EDX);
    __ andl(EBX, Immediate(Selector::ArityField::mask()));

    // Get the receiver from the stack.
    __ negl(EBX);
    __ movl(EBX, Address(EDI, EBX, TIMES_4));
  }

  // Compute the receiver class.
  Label smi, probe;
  ASSERT(Smi::kTag == 0);
  __ testl(EBX, Immediate(Smi::kTagMask));
  __ j(ZERO, &smi);
  __ movl(EBX, Address(EBX, HeapObject::kClassOffset - HeapObject::kTag));

  // Find the entry in the primary lookup cache.
  Label miss, finish;
  ASSERT(Utils::IsPowerOfTwo(LookupCache::kPrimarySize));
  ASSERT(sizeof(LookupCache::Entry) == 1 << 4);
  __ Bind(&probe);
  __ movl(EAX, EBX);
  __ xorl(EAX, EDX);
  __ andl(EAX, Immediate(LookupCache::kPrimarySize - 1));
  __ shll(EAX, Immediate(4));
  __ movl(ECX, Address(EBP, Process::PrimaryLookupCacheOffset()));
  __ addl(EAX, ECX);

  // Validate the primary entry.
  __ cmpl(EBX, Address(EAX, OFFSET_OF(LookupCache::Entry, clazz)));
  __ j(NOT_EQUAL, &miss);
  __ cmpl(EDX, Address(EAX, OFFSET_OF(LookupCache::Entry, selector)));
  __ j(NOT_EQUAL, &miss);

  // At this point, we've got our hands on a valid lookup cache entry.
  Label intrinsified;
  __ Bind(&finish);
  if (test) {
    __ movl(EAX, Address(EAX, OFFSET_OF(LookupCache::Entry, tag)));
  } else {
    __ movl(EBX, Address(EAX, OFFSET_OF(LookupCache::Entry, tag)));
    __ movl(EAX, Address(EAX, OFFSET_OF(LookupCache::Entry, target)));
    __ cmpl(EBX, Immediate(1));
    __ j(ABOVE, &intrinsified);
  }

  if (test) {
    // Materialize either true or false depending on whether or not
    // we've found a target method.
    Label found;
    __ movl(EBX, Address(EBP, Process::ProgramOffset()));
    __ testl(EAX, EAX);
    __ j(NOT_ZERO, &found);

    __ movl(EAX, Address(EBX, Program::false_object_offset()));
    StoreLocal(EAX, 0);
    Dispatch(kInvokeTestLength);

    __ Bind(&found);
    __ movl(EAX, Address(EBX, Program::true_object_offset()));
    StoreLocal(EAX, 0);
    Dispatch(kInvokeTestLength);
  } else {
    // Compute and push the return address on the stack.
    __ addl(ESI, Immediate(kInvokeMethodLength));
    Push(ESI);

    // Jump to the first bytecode in the target method.
    __ leal(ESI, Address(EAX, Function::kSize - HeapObject::kTag));
    CheckStackOverflow(0);
    Dispatch(0);
  }

  __ Bind(&smi);
  __ movl(EBX, Address(EBP, Process::ProgramOffset()));
  __ movl(EBX, Address(EBX, Program::smi_class_offset()));
  __ jmp(&probe);

  if (!test) {
    __ Bind(&intrinsified);
    __ jmp(EBX);
  }

  // We didn't find a valid entry in primary lookup cache.
  __ Bind(&miss);
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), EAX);
  __ movl(Address(ESP, 2 * kWordSize), EBX);
  __ movl(Address(ESP, 3 * kWordSize), EDX);
  __ call("HandleLookupEntry");
  __ jmp(&finish);
}

void InterpreterGeneratorX86::InvokeMethodFast(bool test) {
  // Get the dispatch table and form a pointer to the first element
  // corresponding to this invoke bytecode.
  __ movl(EDX, Address(ESI, 1));
  __ movl(ECX, Address(EBP, Process::ProgramOffset()));
  __ movl(EBX, Address(ECX, Program::DispatchTableOffset()));
  __ leal(EDX, Address(EBX, EDX, TIMES_4, Array::kSize - HeapObject::kTag));

  // Get the receiver from the stack.
  if (test) {
    LoadLocal(EBX, 0);
  } else {
    __ movl(EBX, Address(EDX));  // Get arity.
    __ negl(EBX);
    __ movl(EBX, Address(EDI, EBX, TIMES_2));
  }

  // Compute the receiver class.
  Label smi, probe;
  ASSERT(Smi::kTag == 0);
  __ testl(EBX, Immediate(Smi::kTagMask));
  __ j(ZERO, &smi);
  __ movl(EBX, Address(EBX, HeapObject::kClassOffset - HeapObject::kTag));

  // Fetch the receiver class id and get ready to look at the table entries.
  int id_offset = Class::kIdOrTransformationTargetOffset - HeapObject::kTag;
  __ Bind(&probe);
  __ movl(EBX, Address(EBX, id_offset));

  // Loop through the table.
  Label loop, next;
  __ Bind(&loop);
  __ cmpl(EBX, Address(EDX, 4 * kPointerSize));
  __ j(LESS, &next);
  __ cmpl(EBX, Address(EDX, 5 * kPointerSize));
  __ j(GREATER_EQUAL, &next);

  Label intrinsified;
  if (test) {
    Label false_case, done;
    __ cmpl(Address(EDX, 5 * kPointerSize),
            Immediate(reinterpret_cast<int32>(
                Smi::FromWord(Smi::kMaxPortableValue))));
    __ j(EQUAL, &false_case);
    __ movl(EAX, Address(EBP, Process::ProgramOffset()));
    __ movl(EAX, Address(EAX, Program::true_object_offset()));
    __ jmp(&done);

    __ Bind(&false_case);
    __ movl(EAX, Address(EBP, Process::ProgramOffset()));
    __ movl(EAX, Address(EAX, Program::false_object_offset()));

    __ Bind(&done);
    StoreLocal(EAX, 0);
    Dispatch(kInvokeTestLength);
  } else {
    // Found the right target method.
    __ movl(EBX, Address(EDX, 6 * kPointerSize));
    __ movl(EAX, Address(EDX, 7 * kPointerSize));
    __ testl(EBX, EBX);
    __ j(NOT_ZERO, &intrinsified);

    // Compute and push the return address on the stack.
    __ addl(ESI, Immediate(kInvokeMethodFastLength));
    Push(ESI);

    // Jump to the first bytecode in the target method.
    __ leal(ESI, Address(EAX, Function::kSize - HeapObject::kTag));
    CheckStackOverflow(0);
    Dispatch(0);
  }

  // Go to the next table entry.
  __ Bind(&next);
  __ addl(EDX, Immediate(4 * kPointerSize));
  __ jmp(&loop);

  if (!test) {
    __ Bind(&intrinsified);
    __ jmp(EBX);
  }

  __ Bind(&smi);
  __ movl(EBX, Address(ECX, Program::smi_class_offset()));
  __ jmp(&probe);
}

void InterpreterGeneratorX86::InvokeMethodVtable(bool test) {
  // Get the selector from the bytecodes.
  __ movl(EDX, Address(ESI, 1));

  // Fetch the virtual table from the program.
  __ movl(ECX, Address(EBP, Process::ProgramOffset()));
  __ movl(ECX, Address(ECX, Program::VTableOffset()));

  if (!test) {
    // Compute the arity from the selector.
    ASSERT(Selector::ArityField::shift() == 0);
    __ movl(EBX, EDX);
    __ andl(EBX, Immediate(Selector::ArityField::mask()));
  }

  // Compute the selector offset (smi tagged) from the selector.
  __ andl(EDX, Immediate(Selector::IdField::mask()));
  __ shrl(EDX, Immediate(Selector::IdField::shift() - Smi::kTagSize));

  // Get the receiver from the stack.
  if (test) {
    LoadLocal(EBX, 0);
  } else {
    __ negl(EBX);
    __ movl(EBX, Address(EDI, EBX, TIMES_4));
  }

  // Compute the receiver class.
  Label smi, dispatch;
  ASSERT(Smi::kTag == 0);
  __ testl(EBX, Immediate(Smi::kTagMask));
  __ j(ZERO, &smi);
  __ movl(EBX, Address(EBX, HeapObject::kClassOffset - HeapObject::kTag));

  // Compute entry index: class id + selector offset.
  int id_offset = Class::kIdOrTransformationTargetOffset - HeapObject::kTag;
  __ Bind(&dispatch);
  __ movl(EBX, Address(EBX, id_offset));
  __ addl(EBX, EDX);

  // Fetch the entry from the table. Because the index is smi tagged
  // we only multiply by two -- not four -- when indexing.
  ASSERT(Smi::kTagSize == 1);
  __ movl(ECX, Address(ECX, EBX, TIMES_2, Array::kSize - HeapObject::kTag));

  // Validate that the offset stored in the entry matches the offset
  // we used to find it.
  Label invalid;
  __ cmpl(EDX, Address(ECX, Array::kSize - HeapObject::kTag));
  __ j(NOT_EQUAL, &invalid);

  Label validated, intrinsified;
  if (test) {
    // Valid entry: The answer is true.
    __ movl(EAX, Address(EBP, Process::ProgramOffset()));
    __ movl(EAX, Address(EAX, Program::true_object_offset()));
    StoreLocal(EAX, 0);
    Dispatch(kInvokeTestLength);
  } else {
    // Load the target and the intrinsic from the entry.
    __ Bind(&validated);
    __ movl(EAX, Address(ECX, 8 + Array::kSize - HeapObject::kTag));
    __ movl(EBX, Address(ECX, 12 + Array::kSize - HeapObject::kTag));

    // Check if we have an associated intrinsic.
    __ testl(EBX, EBX);
    __ j(NOT_ZERO, &intrinsified);

    // Compute and push the return address on the stack.
    __ addl(ESI, Immediate(kInvokeMethodVtableLength));
    Push(ESI);

    // Jump to the first bytecode in the target method.
    __ leal(ESI, Address(EAX, Function::kSize - HeapObject::kTag));
    CheckStackOverflow(0);
    Dispatch(0);
  }

  __ Bind(&smi);
  __ movl(EBX, Address(EBP, Process::ProgramOffset()));
  __ movl(EBX, Address(EBX, Program::smi_class_offset()));
  __ jmp(&dispatch);

  if (test) {
    // Invalid entry: The answer is false.
    __ Bind(&invalid);
    __ movl(EAX, Address(EBP, Process::ProgramOffset()));
    __ movl(EAX, Address(EAX, Program::false_object_offset()));
    StoreLocal(EAX, 0);
    Dispatch(kInvokeTestLength);
  } else {
    __ Bind(&intrinsified);
    __ jmp(EBX);

    // Invalid entry: Use the noSuchMethod entry from entry zero of
    // the virtual table.
    __ Bind(&invalid);
    __ movl(ECX, Address(EBP, Process::ProgramOffset()));
    __ movl(ECX, Address(ECX, Program::VTableOffset()));
    __ movl(ECX, Address(ECX, Array::kSize - HeapObject::kTag));
    __ jmp(&validated);
  }
}

void InterpreterGeneratorX86::InvokeStatic(bool unfolded) {
  if (unfolded) {
    __ movl(EAX, Address(ESI, 1));
    __ movl(EAX, Address(ESI, EAX, TIMES_1));
  } else {
    __ movl(EAX, Address(ESI, 1));
    __ movl(EBX, Address(EBP, Process::ProgramOffset()));
    __ movl(EBX, Address(EBX, Program::StaticMethodsOffset()));
    __ movl(EAX, Address(EBX, EAX, TIMES_4, Array::kSize - HeapObject::kTag));
  }

  // Compute and push the return address on the stack.
  __ addl(ESI, Immediate(kInvokeStaticLength));
  Push(ESI);

  // Jump to the first bytecode in the target method.
  __ leal(ESI, Address(EAX, Function::kSize - HeapObject::kTag));
  CheckStackOverflow(0);
  Dispatch(0);
}

void InterpreterGeneratorX86::InvokeCompare(const char* fallback,
                                            Condition condition) {
  LoadLocal(EAX, 0);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(EBX, 1);
  __ testl(EBX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  Label true_case;
  __ cmpl(EBX, EAX);
  __ j(condition, &true_case);

  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::false_object_offset()));
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(5);

  __ Bind(&true_case);
  __ movl(EAX, Address(EBP, Process::ProgramOffset()));
  __ movl(EAX, Address(EAX, Program::true_object_offset()));
  StoreLocal(EAX, 1);
  Drop(1);
  Dispatch(5);
}

void InterpreterGeneratorX86::InvokeDivision(const char* fallback,
                                             bool quotient) {
  LoadLocal(EAX, 1);
  __ testl(EAX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);
  LoadLocal(EBX, 0);
  __ testl(EBX, Immediate(Smi::kTagSize));
  __ j(NOT_ZERO, fallback);

  // Check for division by zero.
  __ testl(EBX, EBX);
  __ j(ZERO, fallback);

  // Untag and sign extend eax into edx:eax.
  __ sarl(EAX, Immediate(1));
  __ sarl(EBX, Immediate(1));
  __ cdq();

  // Divide edx:eax by ebx. The resulting quotient and remainder are in
  // registers eax and edx respectively.
  __ idiv(EBX);

  // Re-tag. We need to check for overflow to handle the case
  // where the top two bits are 01 after the division. This only
  // happens when you divide 0xc0000000 by 0xffffffff.
  ASSERT(Smi::kTagSize == 1 && Smi::kTag == 0);
  Register reg = quotient ? EAX : EDX;
  __ addl(reg, reg);
  __ j(OVERFLOW_, fallback);

  StoreLocal(reg, 1);
  Drop(1);
  Dispatch(kInvokeMethodLength);
}

void InterpreterGeneratorX86::InvokeNative(bool yield) {
  __ movzbl(EBX, Address(ESI, 1));
  __ negl(EBX);
  __ movzbl(EAX, Address(ESI, 2));

  __ LoadNative(EAX, EAX);

  __ leal(EBX, Address(EDI, EBX, TIMES_4));
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), EBX);

  Label failure;
  __ call(EAX);
  __ movl(ECX, EAX);
  __ andl(ECX, Immediate(Failure::kTagMask));
  __ cmpl(ECX, Immediate(Failure::kTag));
  __ j(EQUAL, &failure);

  // Result is in eax. Pointer to first argument is in ebx.
  LoadLocal(ESI, 0);

  if (yield) {
    // Set the result to null and drop the arguments.
    __ movl(ECX, Address(EBP, Process::ProgramOffset()));
    __ movl(ECX, Address(ECX, Program::null_object_offset()));
    __ movl(Address(EBX, 0), ECX);
    __ movl(EDI, EBX);

    // If the result of calling the native is null, we don't yield.
    Label dont_yield;
    __ cmpl(EAX, ECX);
    __ j(EQUAL, &dont_yield);

    // Yield to the target port.
    __ movl(ECX, Address(ESP, 13 * kWordSize));
    __ movl(Address(ECX, 0), EAX);
    __ movl(EAX, Immediate(Interpreter::kTargetYield));
    __ jmp(&done_);
    __ Bind(&dont_yield);
  } else {
    // Store the result in the stack and drop the arguments.
    __ movl(Address(EBX, 0), EAX);
    __ movl(EDI, EBX);
  }

  // Dispatch to return address.
  Dispatch(0);

  // Failure: Check if it's a request to garbage collect. If not,
  // just continue running the failure block by dispatching to the
  // next bytecode.
  __ Bind(&failure);
  __ cmpl(EAX, Immediate(reinterpret_cast<int32>(Failure::retry_after_gc())));
  __ j(EQUAL, &gc_);

  // TODO(kasperl): This should be reworked. We shouldn't be calling
  // through the runtime system for something as simple as converting
  // a failure object to the corresponding heap object.
  __ movl(Address(ESP, 0 * kWordSize), EBP);
  __ movl(Address(ESP, 1 * kWordSize), EAX);
  __ call("HandleObjectFromFailure");

  Push(EAX);
  Dispatch(kInvokeNativeLength);
}

void InterpreterGeneratorX86::CheckStackOverflow(int size) {
  __ movl(EBX, Address(EBP, Process::StackLimitOffset()));
  __ cmpl(EDI, EBX);
  if (size == 0) {
    __ j(ABOVE_EQUAL, &check_stack_overflow_0_);
  } else {
    Label done;
    __ j(BELOW, &done);
    __ movl(EAX, Immediate(size));
    __ jmp(&check_stack_overflow_);
    __ Bind(&done);
  }
}

void InterpreterGeneratorX86::Dispatch(int size) {
  // Load the next bytecode through esi and dispatch to it.
  __ movzbl(EBX, Address(ESI, size));
  if (size > 0) {
    __ addl(ESI, Immediate(size));
  }
  // TODO(kasperl): Let this go through the assembler.
  printf("\tjmp *InterpretFast_DispatchTable(,%%ebx,4)\n");
}

void InterpreterGeneratorX86::SaveState() {
  // Push the bytecode pointer on the stack.
  Push(ESI);

  // Update top in the stack. Ugh. Complicated.
  __ movl(ECX, Address(EBP, Process::CoroutineOffset()));
  __ movl(ECX, Address(ECX, Coroutine::kStackOffset - HeapObject::kTag));
  __ subl(EDI, ECX);
  __ subl(EDI, Immediate(Stack::kSize - HeapObject::kTag));
  __ shrl(EDI, Immediate(1));
  __ movl(Address(ECX, Stack::kTopOffset - HeapObject::kTag), EDI);
}

void InterpreterGeneratorX86::RestoreState() {
  // Load the current stack pointer into edi.
  __ movl(EDI, Address(EBP, Process::CoroutineOffset()));
  __ movl(EDI, Address(EDI, Coroutine::kStackOffset - HeapObject::kTag));
  __ movl(ECX, Address(EDI, Stack::kTopOffset - HeapObject::kTag));
  __ leal(EDI, Address(EDI, ECX, TIMES_2, Stack::kSize - HeapObject::kTag));

  // Pop current bytecode pointer from the stack.
  Pop(ESI);
}

}  // namespace fletch

#endif  // defined FLETCH_TARGET_IA32
