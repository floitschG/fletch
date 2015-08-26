// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#include "src/vm/natives.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "src/shared/bytecodes.h"
#include "src/shared/flags.h"
#include "src/shared/names.h"
#include "src/shared/selectors.h"
#include "src/shared/platform.h"

#include "src/vm/event_handler.h"
#include "src/vm/interpreter.h"
#include "src/vm/port.h"
#include "src/vm/process.h"
#include "src/vm/scheduler.h"
#include "src/vm/session.h"
#include "src/vm/unicode.h"

#include "third_party/double-conversion/src/double-conversion.h"

namespace fletch {

static const char kDoubleExponentChar = 'e';
static const char* kDoubleInfinitySymbol = "Infinity";
static const char* kDoubleNaNSymbol = "NaN";

static Object* ToBool(Process* process, bool value) {
  Program* program = process->program();
  return value ? program->true_object() : program->false_object();
}

NATIVE(PrintToConsole) {
  arguments[0]->ShortPrint();
  Print::Out("\n");
  return process->program()->null_object();
}

NATIVE(ExposeGC) {
  return ToBool(process, Flags::expose_gc);
}

NATIVE(GC) {
#ifdef DEBUG
  // Return a retry_after_gc failure to force a process GC. On the retry return
  // null.
  if (process->TrueThenFalse()) return Failure::retry_after_gc();
#endif
  return process->program()->null_object();
}

NATIVE(IntParse) {
  Object* x = arguments[0];
  if (!x->IsString()) return Failure::wrong_argument_type();
  String* str = String::cast(x);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  Smi* radix = Smi::cast(y);
  char* chars = str->ToCString();
  char* end = chars;
  int64 result = strtoll(chars, &end, radix->value());
  bool error = (end != chars + str->length()) || (errno == ERANGE);
  free(chars);
  if (error) return Failure::index_out_of_bounds();
  return process->ToInteger(result);
}

NATIVE(SmiToDouble) {
  Smi* x = Smi::cast(arguments[0]);
  return process->NewDouble(static_cast<double>(x->value()));
}

NATIVE(SmiToString) {
  Smi* x = Smi::cast(arguments[0]);
  // We need kMaxSmiCharacters + 1 since we need to null terminate the string.
  char buffer[Smi::kMaxSmiCharacters + 1];
  int length = snprintf(buffer, ARRAY_SIZE(buffer), "%ld", x->value());
  ASSERT(length > 0 && length <= Smi::kMaxSmiCharacters);
  return process->NewStringFromAscii(List<const char>(buffer, length));
}

NATIVE(SmiToMint) {
  Smi* x = Smi::cast(arguments[0]);
  return process->NewInteger(x->value());
}

NATIVE(SmiNegate) {
  Smi* x = Smi::cast(arguments[0]);
  return Smi::FromWord(-x->value());
}

NATIVE(SmiAdd) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word x_value = reinterpret_cast<word>(x);
  word y_value = reinterpret_cast<word>(y);
  word result;
  if (Utils::SignedAddOverflow(x_value, y_value, &result)) {
    // TODO(kasperl): Consider throwing a different error on overflow?
    return Failure::wrong_argument_type();
  }
  return reinterpret_cast<Smi*>(result);
}

NATIVE(SmiSub) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word x_value = reinterpret_cast<word>(x);
  word y_value = reinterpret_cast<word>(y);
  word result;
  if (Utils::SignedSubOverflow(x_value, y_value, &result)) {
    // TODO(kasperl): Consider throwing a different error on overflow?
    return Failure::wrong_argument_type();
  }
  return reinterpret_cast<Smi*>(result);
}

NATIVE(SmiMul) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word x_value = reinterpret_cast<word>(x);
  word y_value = Smi::cast(y)->value();
  word result;
  if (Utils::SignedMulOverflow(x_value, y_value, &result)) {
    // TODO(kasperl): Consider throwing a different error on overflow?
    return Failure::wrong_argument_type();
  }
  return reinterpret_cast<Smi*>(result);
}

NATIVE(SmiMod) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  if (y_value == 0) return Failure::index_out_of_bounds();
  word result = x->value() % y_value;
  if (result < 0) result += (y_value > 0) ? y_value : -y_value;
  return Smi::FromWord(result);
}

NATIVE(SmiDiv) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  return process->NewDouble(static_cast<double>(x->value()) / y_value);
}

NATIVE(SmiTruncDiv) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  if (y_value == 0) return Failure::index_out_of_bounds();
  word result = x->value() / y_value;
  if (!Smi::IsValid(result)) return Failure::wrong_argument_type();
  return Smi::FromWord(result);
}

NATIVE(SmiBitNot) {
  Smi* x = Smi::cast(arguments[0]);
  return Smi::FromWord(~x->value());
}

NATIVE(SmiBitAnd) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  return Smi::FromWord(x->value() & y_value);
}

NATIVE(SmiBitOr) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  return Smi::FromWord(x->value() | y_value);
}

NATIVE(SmiBitXor) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  return Smi::FromWord(x->value() ^ y_value);
}

NATIVE(SmiBitShr) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  return Smi::FromWord(x->value() >> y_value);
}

NATIVE(SmiBitShl) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  if (y_value >= kBitsPerPointer) return Failure::wrong_argument_type();
  word x_value = x->value();
  word result = x_value << y_value;
  bool overflow = !Smi::IsValid(result) || ((result >> y_value) != x_value);
  if (overflow) return Failure::wrong_argument_type();
  return Smi::FromWord(result);
}

NATIVE(SmiEqual) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  return ToBool(process, x == y);
}

NATIVE(SmiLess) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  return ToBool(process, x->value() < y_value);
}

NATIVE(SmiLessEqual) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  return ToBool(process, x->value() <= y_value);
}

NATIVE(SmiGreater) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  return ToBool(process, x->value() > y_value);
}

NATIVE(SmiGreaterEqual) {
  Smi* x = Smi::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word y_value = Smi::cast(y)->value();
  return ToBool(process, x->value() >= y_value);
}

NATIVE(MintToDouble) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  return process->NewDouble(static_cast<double>(x->value()));
}

NATIVE(MintToString) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  long long int value = x->value();  // NOLINT
  char buffer[128];  // TODO(kasperl): What's the right buffer size?
  int length = snprintf(buffer, ARRAY_SIZE(buffer), "%lld", value);
  return process->NewStringFromAscii(List<const char>(buffer, length));
}

NATIVE(MintNegate) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  return process->NewInteger(-x->value());
}

NATIVE(MintAdd) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  // TODO(kasperl): Check for overflow.
  return process->ToInteger(x->value() + y_value);
}

NATIVE(MintSub) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  // TODO(kasperl): Check for overflow.
  return process->ToInteger(x->value() - y_value);
}

NATIVE(MintMul) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  // TODO(kasperl): Check for overflow.
  return process->ToInteger(x->value() * y_value);
}

NATIVE(MintMod) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  if (y_value == 0 ||
      (y_value == -1 && x->value() == (-1LL << 63))) {
    return Failure::index_out_of_bounds();
  }
  int64 result = x->value() % y_value;
  if (result < 0) result += (y_value > 0) ? y_value : -y_value;
  return process->ToInteger(result);
}

NATIVE(MintDiv) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return process->NewDouble(static_cast<double>(x->value()) / y_value);
}

NATIVE(MintTruncDiv) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  if (y_value == 0 ||
      (y_value == -1 && x->value() == (-1LL << 63))) {
    return Failure::index_out_of_bounds();
  }
  return process->ToInteger(x->value() / y_value);
}

NATIVE(MintBitNot) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  return process->NewInteger(~x->value());
}

NATIVE(MintBitAnd) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return process->ToInteger(x->value() & y_value);
}

NATIVE(MintBitOr) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return process->ToInteger(x->value() | y_value);
}

NATIVE(MintBitXor) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return process->ToInteger(x->value() ^ y_value);
}

NATIVE(MintBitShr) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return process->ToInteger(x->value() >> y_value);
}

NATIVE(MintBitShl) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  if (y_value >= 64) return Failure::wrong_argument_type();
  return process->ToInteger(x->value() << y_value);
}

NATIVE(MintEqual) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return ToBool(process, x->value() == y_value);
}

NATIVE(MintLess) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return ToBool(process, x->value() < y_value);
}

NATIVE(MintLessEqual) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return ToBool(process, x->value() <= y_value);
}

NATIVE(MintGreater) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return ToBool(process, x->value() > y_value);
}

NATIVE(MintGreaterEqual) {
  LargeInteger* x = LargeInteger::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsLargeInteger()) return Failure::wrong_argument_type();
  int64 y_value = LargeInteger::cast(y)->value();
  return ToBool(process, x->value() >= y_value);
}

NATIVE(DoubleNegate) {
  Double* x = Double::cast(arguments[0]);
  return process->NewDouble(-x->value());
}

NATIVE(DoubleAdd) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = Double::cast(y)->value();
  return process->NewDouble(x->value() + y_value);
}

NATIVE(DoubleSub) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = Double::cast(y)->value();
  return process->NewDouble(x->value() - y_value);
}

NATIVE(DoubleMul) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = Double::cast(y)->value();
  return process->NewDouble(x->value() * y_value);
}

NATIVE(DoubleMod) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = Double::cast(y)->value();
  return process->NewDouble(fmod(x->value(), y_value));
}

NATIVE(DoubleDiv) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = Double::cast(y)->value();
  return process->NewDouble(x->value() / y_value);
}

NATIVE(DoubleTruncDiv) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = Double::cast(y)->value();
  if (y_value == 0) return Failure::index_out_of_bounds();
  return process->NewInteger(static_cast<int64>(x->value() / y_value));
}

NATIVE(DoubleEqual) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = static_cast<double>(Double::cast(y)->value());
  return ToBool(process, x->value() == y_value);
}

NATIVE(DoubleLess) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = static_cast<double>(Double::cast(y)->value());
  return ToBool(process, x->value() < y_value);
}

NATIVE(DoubleLessEqual) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = static_cast<double>(Double::cast(y)->value());
  return ToBool(process, x->value() <= y_value);
}

NATIVE(DoubleGreater) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = static_cast<double>(Double::cast(y)->value());
  return ToBool(process, x->value() > y_value);
}

NATIVE(DoubleGreaterEqual) {
  Double* x = Double::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double y_value = static_cast<double>(Double::cast(y)->value());
  return ToBool(process, x->value() >= y_value);
}

NATIVE(DoubleIsNaN) {
  double d = Double::cast(arguments[0])->value();
  return ToBool(process, isnan(d));
}

NATIVE(DoubleIsNegative) {
  double d = Double::cast(arguments[0])->value();
  return ToBool(process, (signbit(d) != 0) && !isnan(d));
}

NATIVE(DoubleCeil) {
  double value = Double::cast(arguments[0])->value();
  if (isnan(value) || isinf(value)) return Failure::index_out_of_bounds();
  return process->ToInteger(static_cast<int64>(ceil(value)));
}

NATIVE(DoubleCeilToDouble) {
  Double* x = Double::cast(arguments[0]);
  return process->NewDouble(ceil(x->value()));
}

NATIVE(DoubleRound) {
  double value = Double::cast(arguments[0])->value();
  if (isnan(value) || isinf(value)) return Failure::index_out_of_bounds();
  return process->ToInteger(static_cast<int64>(round(value)));
}

NATIVE(DoubleRoundToDouble) {
  Double* x = Double::cast(arguments[0]);
  return process->NewDouble(round(x->value()));
}

NATIVE(DoubleFloor) {
  double value = Double::cast(arguments[0])->value();
  if (isnan(value) || isinf(value)) return Failure::index_out_of_bounds();
  return process->ToInteger(static_cast<int64>(floor(value)));
}

NATIVE(DoubleFloorToDouble) {
  Double* x = Double::cast(arguments[0]);
  return process->NewDouble(floor(x->value()));
}

NATIVE(DoubleTruncate) {
  double value = Double::cast(arguments[0])->value();
  if (isnan(value) || isinf(value)) return Failure::index_out_of_bounds();
  return process->ToInteger(static_cast<int64>(trunc(value)));
}

NATIVE(DoubleTruncateToDouble) {
  Double* x = Double::cast(arguments[0]);
  return process->NewDouble(trunc(x->value()));
}

NATIVE(DoubleRemainder) {
  if (!arguments[1]->IsDouble()) return Failure::wrong_argument_type();
  Double* x = Double::cast(arguments[0]);
  Double* y = Double::cast(arguments[1]);
  return process->NewDouble(fmod(x->value(), y->value()));
}

NATIVE(DoubleToInt) {
  double d = Double::cast(arguments[0])->value();
  if (isinf(d) || isnan(d)) return Failure::index_out_of_bounds();
  // TODO(ager): Handle large doubles that are out of int64 range.
  int64 result = static_cast<int64>(trunc(d));
  return process->ToInteger(result);
}

NATIVE(DoubleToString) {
  static const int kDecimalLow = -6;
  static const int kDecimalHigh = 21;
  static const int kBufferSize = 128;

  Double* d = Double::cast(arguments[0]);
  char buffer[kBufferSize] = { '\0' };

  // The output contains the sign, at most kDecimalHigh - 1 digits,
  // the decimal point followed by a 0 plus the \0.
  ASSERT(kBufferSize >= 1 + (kDecimalHigh - 1) + 1 + 1 + 1);
  // Or it contains the sign, a 0, the decimal point, kDecimalLow '0's,
  // 17 digits (the precision needed for doubles), plus the \0.
  ASSERT(kBufferSize >= 1 + 1 + 1 + kDecimalLow + 17 + 1);
  // Alternatively it contains a sign, at most 17 digits (precision needed for
  // any double), the decimal point, the exponent character, the exponent's
  // sign, at most three exponent digits, plus the \0.
  ASSERT(kBufferSize >= 1 + 17 + 1 + 1 + 1 + 3 + 1);

  static const int kConversionFlags =
    double_conversion::DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN |
    double_conversion::DoubleToStringConverter::EMIT_TRAILING_DECIMAL_POINT |
    double_conversion::DoubleToStringConverter::EMIT_TRAILING_ZERO_AFTER_POINT;

  const double_conversion::DoubleToStringConverter converter(
      kConversionFlags,
      kDoubleInfinitySymbol,
      kDoubleNaNSymbol,
      kDoubleExponentChar,
      kDecimalLow,
      kDecimalHigh,
      0, 0);  // Last two values are ignored in shortest mode.

  double_conversion::StringBuilder builder(buffer, kBufferSize);
  bool status = converter.ToShortest(d->value(), &builder);
  ASSERT(status);
  char* result = builder.Finalize();
  ASSERT(result == buffer);
  return process->NewStringFromAscii(List<const char>(result, strlen(result)));
}

NATIVE(DoubleToStringAsExponential) {
  static const int kBufferSize = 128;

  double d = Double::cast(arguments[0])->value();
  int digits = Smi::cast(arguments[1])->value();
  ASSERT(-1 <= digits && digits <= 20);

  const double_conversion::DoubleToStringConverter converter(
      double_conversion::DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN,
      kDoubleInfinitySymbol,
      kDoubleNaNSymbol,
      kDoubleExponentChar,
      0, 0, 0, 0);  // Last four values are ignored in exponential mode.

  char buffer[kBufferSize] = { '\0' };
  double_conversion::StringBuilder builder(buffer, kBufferSize);
  bool status = converter.ToExponential(d, digits, &builder);
  ASSERT(status);
  char* result = builder.Finalize();
  ASSERT(result == buffer);
  return process->NewStringFromAscii(List<const char>(result, strlen(result)));
}

NATIVE(DoubleToStringAsFixed) {
  static const int kBufferSize = 128;

  double d = Double::cast(arguments[0])->value();
  ASSERT(-1e21 <= d && d <= 1e21);
  int digits = Smi::cast(arguments[1])->value();
  ASSERT(0 <= digits && digits <= 20);

  const double_conversion::DoubleToStringConverter converter(
      double_conversion::DoubleToStringConverter::NO_FLAGS,
      kDoubleInfinitySymbol,
      kDoubleNaNSymbol,
      kDoubleExponentChar,
      0, 0, 0, 0);  // Last four values are ignored in fixed mode.

  char buffer[kBufferSize] = { '\0' };
  double_conversion::StringBuilder builder(buffer, kBufferSize);
  bool status = converter.ToFixed(d, digits, &builder);
  ASSERT(status);
  char* result = builder.Finalize();
  ASSERT(result == buffer);
  return process->NewStringFromAscii(List<const char>(result, strlen(result)));
}

NATIVE(DoubleToStringAsPrecision) {
  static const int kBufferSize = 128;
  static const int kMaxLeadingPaddingZeroes = 6;
  static const int kMaxTrailingPaddingZeroes = 0;

  double d = Double::cast(arguments[0])->value();
  int digits = Smi::cast(arguments[1])->value();
  ASSERT(1 <= digits && digits <= 21);

  const double_conversion::DoubleToStringConverter converter(
      double_conversion::DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN,
      kDoubleInfinitySymbol,
      kDoubleNaNSymbol,
      kDoubleExponentChar,
      0, 0,  // Ignored in precision mode.
      kMaxLeadingPaddingZeroes,
      kMaxTrailingPaddingZeroes);

  char buffer[kBufferSize] = { '\0' };
  double_conversion::StringBuilder builder(buffer, kBufferSize);
  bool status = converter.ToPrecision(d, digits, &builder);
  ASSERT(status);
  char* result = builder.Finalize();
  ASSERT(result == buffer);
  return process->NewStringFromAscii(List<const char>(result, strlen(result)));
}

NATIVE(DoubleParse) {
  Object* x = arguments[0];
  if (!x->IsString()) return Failure::wrong_argument_type();

  // We trim in Dart to handle all the whitespaces.
  static const int kConversionFlags =
      double_conversion::StringToDoubleConverter::NO_FLAGS;

  double_conversion::StringToDoubleConverter converter(
      kConversionFlags,
      0.0,
      0.0,
      kDoubleInfinitySymbol,
      kDoubleNaNSymbol);

  String* source = String::cast(x);
  int length = source->length();
  uint16* buffer = reinterpret_cast<uint16_t*>(source->byte_address_for(0));
  int consumed = 0;
  double result = converter.StringToDouble(buffer, length, &consumed);

  // The string is trimmed, so we must accept the full string.
  if (consumed != length) return Failure::index_out_of_bounds();
  return process->NewDouble(result);
}

#define DOUBLE_MATH_NATIVE(name, method)                        \
  NATIVE(name) {                                                \
    Object* x = arguments[0];                                   \
    if (!x->IsDouble()) return Failure::wrong_argument_type();  \
    double d = Double::cast(x)->value();                        \
    return process->NewDouble(method(d));                       \
  }

DOUBLE_MATH_NATIVE(DoubleSin, sin)
DOUBLE_MATH_NATIVE(DoubleCos, cos)
DOUBLE_MATH_NATIVE(DoubleTan, tan)
DOUBLE_MATH_NATIVE(DoubleAcos, acos)
DOUBLE_MATH_NATIVE(DoubleAsin, asin)
DOUBLE_MATH_NATIVE(DoubleAtan, atan)
DOUBLE_MATH_NATIVE(DoubleSqrt, sqrt)
DOUBLE_MATH_NATIVE(DoubleExp, exp)
DOUBLE_MATH_NATIVE(DoubleLog, log)

NATIVE(DoubleAtan2) {
  Object* x = arguments[0];
  if (!x->IsDouble()) return Failure::wrong_argument_type();
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double x_value = Double::cast(x)->value();
  double y_value = Double::cast(y)->value();
  return process->NewDouble(atan2(x_value, y_value));
}

NATIVE(DoublePow) {
  Object* x = arguments[0];
  if (!x->IsDouble()) return Failure::wrong_argument_type();
  Object* y = arguments[1];
  if (!y->IsDouble()) return Failure::wrong_argument_type();
  double x_value = Double::cast(x)->value();
  double y_value = Double::cast(y)->value();
  return process->NewDouble(pow(x_value, y_value));
}

NATIVE(ListNew) {
  Object* x = arguments[0];
  if (!x->IsSmi()) return Failure::wrong_argument_type();
  int length = Smi::cast(arguments[0])->value();
  if (length < 0) return Failure::index_out_of_bounds();
  return process->NewArray(length);
}

NATIVE(ListLength) {
  Object* list = Instance::cast(arguments[0])->GetInstanceField(0);
  return Smi::FromWord(BaseArray::cast(list)->length());
}

NATIVE(ListIndexGet) {
  Object* list = Instance::cast(arguments[0])->GetInstanceField(0);
  Array* array = Array::cast(list);
  Object* x = arguments[1];
  if (!x->IsSmi()) return Failure::wrong_argument_type();
  int index = Smi::cast(x)->value();
  if (index < 0 || index >= array->length()) {
    return Failure::index_out_of_bounds();
  }
  return array->get(index);
}

NATIVE(ByteListIndexGet) {
  Object* list = Instance::cast(arguments[0])->GetInstanceField(0);
  ByteArray* array = ByteArray::cast(list);
  Object* x = arguments[1];
  if (!x->IsSmi()) return Failure::wrong_argument_type();
  int index = Smi::cast(x)->value();
  if (index < 0 || index >= array->length()) {
    return Failure::index_out_of_bounds();
  }
  return Smi::FromWord(array->get(index));
}

NATIVE(ListIndexSet) {
  Object* list = Instance::cast(arguments[0])->GetInstanceField(0);
  Array* array = Array::cast(list);
  Object* x = arguments[1];
  if (!x->IsSmi()) return Failure::wrong_argument_type();
  int index = Smi::cast(x)->value();
  if (index < 0 || index >= array->length()) {
    return Failure::index_out_of_bounds();
  }
  Object* value = arguments[2];
  array->set(index, value);
  process->RecordStore(array, value);
  return value;
}

static Function* FunctionForClosure(Object* argument, unsigned arity) {
  Instance* closure = Instance::cast(argument);
  Class* closure_class = closure->get_class();
  word selector = Selector::EncodeMethod(Names::kCall, arity);
  return closure_class->LookupMethod(selector);
}

NATIVE(ProcessSpawn) {
  Program* program = process->program();

  Instance* entrypoint = Instance::cast(arguments[0]);
  Instance* closure = Instance::cast(arguments[1]);
  Object* argument = arguments[2];

  if (!closure->IsImmutable()) {
    // TODO(kasperl): Return a proper failure.
    return Failure::index_out_of_bounds();
  }

  bool has_argument = !argument->IsNull();
  if (has_argument && !argument->IsImmutable()) {
    // TODO(kasperl): Return a proper failure.
    return Failure::index_out_of_bounds();
  }

  if (FunctionForClosure(closure, has_argument ? 1 : 0) == NULL) {
    // TODO(kasperl): Return a proper failure.
    return Failure::index_out_of_bounds();
  }

  Function* entry = FunctionForClosure(entrypoint, 2);
  ASSERT(entry != NULL);

  // Spawn a new process and create a copy of the closure in the
  // new process' heap.
  Process* child = program->SpawnProcess();

  // Set up the stack as a call of the entry with one argument: closure.
  child->SetupExecutionStack();
  Stack* stack = child->stack();
  uint8_t* bcp = entry->bytecode_address_for(0);
  // The entry closure takes three arguments, 'this', the closure, and
  // a single argument. Since the method is a static tear-off, 'this'
  // is not used and simply be 'NULL'.
  stack->set(0, NULL);
  stack->set(1, closure);
  stack->set(2, argument);
  // Push 'NULL' return address. This will tell the stack-walker this is the
  // last function.
  stack->set(3, NULL);
  // Finally push the bcp.
  stack->set(4, reinterpret_cast<Object*>(bcp));
  stack->set_top(4);

  program->scheduler()->EnqueueProcessOnSchedulerWorkerThread(
      process, child);
  return process->program()->null_object();
}

NATIVE(CoroutineCurrent) {
  return process->coroutine();
}

NATIVE(CoroutineNewStack) {
  Object* object = process->NewStack(256);
  if (object->IsFailure()) return object;
  Instance* coroutine = Instance::cast(arguments[0]);
  Instance* entry = Instance::cast(arguments[1]);

  // TODO(kasperl): Avoid repeated lookups. Cache the start
  // function in the program?
  int selector = Selector::Encode(Names::kCoroutineStart, Selector::METHOD, 1);
  Function* start = coroutine->get_class()->LookupMethod(selector);
  ASSERT(start->arity() == 2);

  uint8* bcp = start->bytecode_address_for(0);
  ASSERT(bcp[0] == kLoadLiteral0);
  ASSERT(bcp[1] == kLoadLiteral0);
  ASSERT(bcp[2] == kCoroutineChange);

  Stack* stack = Stack::cast(object);
  stack->set(0, coroutine);
  stack->set(1, entry);
  stack->set(2, NULL);  // Terminating return address.
  stack->set(3, Smi::FromWord(0));  // Fake 'stack' argument.
  stack->set(4, Smi::FromWord(0));  // Fake 'value' argument.
  // Leave bcp at the kChangeStack instruction to make it look like a
  // suspended co-routine. bcp is incremented on resume.
  stack->set(5, reinterpret_cast<Object*>(bcp + 2));
  stack->set_top(5);
  return stack;
}

NATIVE(StopwatchFrequency) {
  return Smi::FromWord(1000000);
}

NATIVE(StopwatchNow) {
  static uint64 first = 0;
  uint64 now = Platform::GetProcessMicroseconds();
  if (first == 0) first = now;
  return process->ToInteger(now - first);
}

NATIVE(IdentityHashCode) {
  Object* object = arguments[0];
  if (object->IsString()) {
    return Smi::FromWord(String::cast(object)->Hash());
  } else if (object->IsSmi() || object->IsLargeInteger()) {
    return object;
  } else if (object->IsDouble()) {
    double value = Double::cast(object)->value();
    return process->ToInteger(static_cast<int64>(value));
  } else {
    return ComplexHeapObject::cast(object)->LazyIdentityHashCode(
        process->random());
  }
}

char* AsForeignString(String* s) {
  return s->ToCString();
}

NATIVE(StringLength) {
  String* x = String::cast(arguments[0]);
  return Smi::FromWord(x->length());
}

NATIVE(StringAdd) {
  String* x = String::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsString()) return Failure::wrong_argument_type();
  return process->Concatenate(x, String::cast(y));
}

NATIVE(StringCodeUnitAt) {
  String* x = String::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word index = Smi::cast(y)->value();
  if (index < 0) return Failure::index_out_of_bounds();
  if (index >= x->length()) return Failure::index_out_of_bounds();
  return process->ToInteger(x->get_code_unit(index));
}

NATIVE(StringCreate) {
  Object* z = arguments[0];
  if (!z->IsSmi()) return Failure::wrong_argument_type();
  word length = Smi::cast(z)->value();
  if (length < 0) return Failure::index_out_of_bounds();
  Object* result = process->NewString(length);
  return result;
}

NATIVE(StringEqual) {
  String* x = String::cast(arguments[0]);
  Object* y = arguments[1];
  return ToBool(process, y->IsString() && x->Equals(String::cast(y)));
}

NATIVE(StringSetCodeUnitAt) {
  String* x = String::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  word index = Smi::cast(y)->value();
  if (index < 0) return Failure::index_out_of_bounds();
  Object* z = arguments[2];
  if (!z->IsSmi()) return Failure::wrong_argument_type();
  word value = Smi::cast(z)->value();
  if (value < 0 || value > 65535) return Failure::wrong_argument_type();
  x->set_code_unit(index, value);
  return process->program()->null_object();
}

NATIVE(StringSubstring) {
  String* x = String::cast(arguments[0]);
  Object* y = arguments[1];
  if (!y->IsSmi()) return Failure::wrong_argument_type();
  Object* z = arguments[2];
  if (!z->IsSmi()) return Failure::wrong_argument_type();
  word start = Smi::cast(y)->value();
  word end = Smi::cast(z)->value();
  if (start < 0) return Failure::index_out_of_bounds();
  if (end < start) return Failure::index_out_of_bounds();
  int length = x->length();
  if (end > length) return Failure::index_out_of_bounds();
  if (start == 0 && end == length) return x;
  int substring_length = end - start;
  int data_size = substring_length * sizeof(uint16_t);
  Object* raw_string = process->NewStringUninitialized(substring_length);
  if (raw_string->IsFailure()) return raw_string;
  String* result = String::cast(raw_string);
  memcpy(result->byte_address_for(0), x->byte_address_for(start), data_size);
  return result;
}

NATIVE(DateTimeGetCurrentMs) {
  uint64 us = Platform::GetMicroseconds();
  return process->ToInteger(us / 1000);
}

static int64 kMaxTimeZoneOffsetSeconds = 2100000000;

NATIVE(DateTimeTimeZone) {
  word seconds = AsForeignWord(arguments[0]);
  if (seconds < 0 || seconds > kMaxTimeZoneOffsetSeconds) {
    return Failure::index_out_of_bounds();
  }
  const char* name = Platform::GetTimeZoneName(seconds);
  return process->NewStringFromAscii(List<const char>(name, strlen(name)));
}

NATIVE(DateTimeTimeZoneOffset) {
  word seconds = AsForeignWord(arguments[0]);
  if (seconds < 0 || seconds > kMaxTimeZoneOffsetSeconds) {
    return Failure::index_out_of_bounds();
  }
  int offset = Platform::GetTimeZoneOffset(seconds);
  return process->ToInteger(offset);
}

NATIVE(DateTimeLocalTimeZoneOffset) {
  int offset = Platform::GetLocalTimeZoneOffset();
  return process->ToInteger(offset);
}

NATIVE(UriBase) {
  char* buffer = reinterpret_cast<char*>(malloc(PATH_MAX + 1));
  char* path = getcwd(buffer, PATH_MAX);
  Object* result = Failure::index_out_of_bounds();
  if (path != NULL) {
    int length = strlen(path);
    path[length++] = '/';
    path[length] = 0;
    result = process->NewStringFromAscii(List<const char>(path, length));
  }
  free(buffer);
  return result;
}

NATIVE(SystemGetEventHandler) {
  int fd = process->program()->event_handler()->GetEventHandler();
  return process->ToInteger(fd);
}

NATIVE(IsImmutable) {
  Object* o = arguments[0];
  return ToBool(process, o->IsImmutable());
}

}  // namespace fletch
