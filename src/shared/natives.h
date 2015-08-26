// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#ifndef SRC_SHARED_NATIVES_H_
#define SRC_SHARED_NATIVES_H_

namespace fletch {

#define NATIVES_DO(N)                                                    \
  N(PrintToConsole,              "<none>", "printToConsole")             \
  N(ExposeGC,                    "<none>", "_exposeGC")                  \
  N(GC,                          "<none>", "_gc")                        \
                                                                         \
  N(IntParse,                    "int",    "_parse")                     \
                                                                         \
  N(SmiToDouble,                 "_Smi",   "toDouble")                   \
  N(SmiToString,                 "_Smi",   "toString")                   \
  N(SmiToMint,                   "_Smi",   "_toMint")                    \
                                                                         \
  N(SmiNegate,                   "_Smi",   "unary-")                     \
                                                                         \
  N(SmiAdd,                      "_Smi",   "+")                          \
  N(SmiSub,                      "_Smi",   "-")                          \
  N(SmiMul,                      "_Smi",   "*")                          \
  N(SmiMod,                      "_Smi",   "%")                          \
  N(SmiDiv,                      "_Smi",   "/")                          \
  N(SmiTruncDiv,                 "_Smi",   "~/")                         \
                                                                         \
  N(SmiBitNot,                   "_Smi",   "~")                          \
  N(SmiBitAnd,                   "_Smi",   "&")                          \
  N(SmiBitOr,                    "_Smi",   "|")                          \
  N(SmiBitXor,                   "_Smi",   "^")                          \
  N(SmiBitShr,                   "_Smi",   ">>")                         \
  N(SmiBitShl,                   "_Smi",   "<<")                         \
                                                                         \
  N(SmiEqual,                    "_Smi",   "==")                         \
  N(SmiLess,                     "_Smi",   "<")                          \
  N(SmiLessEqual,                "_Smi",   "<=")                         \
  N(SmiGreater,                  "_Smi",   ">")                          \
  N(SmiGreaterEqual,             "_Smi",   ">=")                         \
                                                                         \
  N(MintToDouble,                "_Mint",  "toDouble")                   \
  N(MintToString,                "_Mint",  "toString")                   \
                                                                         \
  N(MintNegate,                  "_Mint",  "unary-")                     \
                                                                         \
  N(MintAdd,                     "_Mint",  "+")                          \
  N(MintSub,                     "_Mint",  "-")                          \
  N(MintMul,                     "_Mint",  "*")                          \
  N(MintMod,                     "_Mint",  "%")                          \
  N(MintDiv,                     "_Mint",  "/")                          \
  N(MintTruncDiv,                "_Mint",  "~/")                         \
                                                                         \
  N(MintBitNot,                  "_Mint",  "~")                          \
  N(MintBitAnd,                  "_Mint",  "&")                          \
  N(MintBitOr,                   "_Mint",  "|")                          \
  N(MintBitXor,                  "_Mint",  "^")                          \
  N(MintBitShr,                  "_Mint",  ">>")                         \
  N(MintBitShl,                  "_Mint",  "<<")                         \
                                                                         \
  N(MintEqual,                   "_Mint",  "==")                         \
  N(MintLess,                    "_Mint",  "<")                          \
  N(MintLessEqual,               "_Mint",  "<=")                         \
  N(MintGreater,                 "_Mint",  ">")                          \
  N(MintGreaterEqual,            "_Mint",  ">=")                         \
                                                                         \
  N(DoubleNegate,                "_DoubleImpl", "unary-")                \
                                                                         \
  N(DoubleAdd,                   "_DoubleImpl", "+")                     \
  N(DoubleSub,                   "_DoubleImpl", "-")                     \
  N(DoubleMul,                   "_DoubleImpl", "*")                     \
  N(DoubleMod,                   "_DoubleImpl", "%")                     \
  N(DoubleDiv,                   "_DoubleImpl", "/")                     \
  N(DoubleTruncDiv,              "_DoubleImpl", "~/")                    \
                                                                         \
  N(DoubleEqual,                 "_DoubleImpl", "==")                    \
  N(DoubleLess,                  "_DoubleImpl", "<")                     \
  N(DoubleLessEqual,             "_DoubleImpl", "<=")                    \
  N(DoubleGreater,               "_DoubleImpl", ">")                     \
  N(DoubleGreaterEqual,          "_DoubleImpl", ">=")                    \
                                                                         \
  N(DoubleIsNaN,                 "_DoubleImpl", "isNaN")                 \
  N(DoubleIsNegative,            "_DoubleImpl", "isNegative")            \
                                                                         \
  N(DoubleCeil,                  "_DoubleImpl", "ceil")                  \
  N(DoubleCeilToDouble,          "_DoubleImpl", "ceilToDouble")          \
  N(DoubleRound,                 "_DoubleImpl", "round")                 \
  N(DoubleRoundToDouble,         "_DoubleImpl", "roundToDouble")         \
  N(DoubleFloor,                 "_DoubleImpl", "floor")                 \
  N(DoubleFloorToDouble,         "_DoubleImpl", "floorToDouble")         \
  N(DoubleTruncate,              "_DoubleImpl", "truncate")              \
  N(DoubleTruncateToDouble,      "_DoubleImpl", "truncateToDouble")      \
  N(DoubleRemainder,             "_DoubleImpl", "remainder")             \
  N(DoubleToInt,                 "_DoubleImpl", "toInt")                 \
  N(DoubleToString,              "_DoubleImpl", "toString")              \
  N(DoubleToStringAsExponential, "_DoubleImpl", "_toStringAsExponential")\
  N(DoubleToStringAsFixed,       "_DoubleImpl", "_toStringAsFixed")      \
  N(DoubleToStringAsPrecision,   "_DoubleImpl", "_toStringAsPrecision")  \
                                                                         \
  N(DoubleParse,                 "double", "_parse")                     \
                                                                         \
  N(DoubleSin,                   "<none>", "_sin")                       \
  N(DoubleCos,                   "<none>", "_cos")                       \
  N(DoubleTan,                   "<none>", "_tan")                       \
  N(DoubleAcos,                  "<none>", "_acos")                      \
  N(DoubleAsin,                  "<none>", "_asin")                      \
  N(DoubleAtan,                  "<none>", "_atan")                      \
  N(DoubleSqrt,                  "<none>", "_sqrt")                      \
  N(DoubleExp,                   "<none>", "_exp")                       \
  N(DoubleLog,                   "<none>", "_log")                       \
  N(DoubleAtan2,                 "<none>", "_atan2")                     \
  N(DoublePow,                   "<none>", "_pow")                       \
                                                                         \
  N(DateTimeGetCurrentMs,        "DateTime", "_getCurrentMs")            \
  N(DateTimeTimeZone,            "DateTime", "_timeZone")                \
  N(DateTimeTimeZoneOffset,      "DateTime", "_timeZoneOffset")          \
  N(DateTimeLocalTimeZoneOffset, "DateTime", "_localTimeZoneOffset")     \
                                                                         \
  N(ListNew,                     "_FixedListBase", "_new")               \
  N(ListLength,                  "_FixedListBase", "length")             \
  N(ListIndexGet,                "_FixedListBase", "[]")                 \
                                                                         \
  N(ByteListIndexGet,            "_ConstantByteList", "[]")              \
                                                                         \
  N(ListIndexSet,                "_FixedList", "[]=")                    \
                                                                         \
  N(ProcessSpawn,                "Process", "_spawn")                    \
  N(ProcessQueueGetMessage,      "Process", "_queueGetMessage")          \
  N(ProcessQueueGetChannel,      "Process", "_queueGetChannel")          \
                                                                         \
  N(CoroutineCurrent,            "Coroutine", "_coroutineCurrent")       \
  N(CoroutineNewStack,           "Coroutine", "_coroutineNewStack")      \
                                                                         \
  N(StopwatchFrequency,          "Stopwatch", "_frequency")              \
  N(StopwatchNow,                "Stopwatch", "_now")                    \
                                                                         \
  N(ForeignLibraryLookup,        "ForeignLibrary", "_lookupLibrary")     \
  N(ForeignLibraryClose,         "ForeignLibrary", "_closeLibrary")      \
  N(ForeignLibraryGetFunction,   "ForeignLibrary", "_lookupFunction")    \
  N(ForeignLibraryBundlePath,    "ForeignLibrary", "bundleLibraryName")  \
                                                                         \
  N(ForeignBitsPerWord,          "Foreign", "_bitsPerMachineWord")       \
  N(ForeignErrno,                "Foreign", "_errno")                    \
  N(ForeignPlatform,             "Foreign", "_platform")                 \
  N(ForeignArchitecture,         "Foreign", "_architecture")             \
  N(ForeignConvertPort,          "Foreign", "_convertPort")              \
                                                                         \
  N(ForeignICall0,               "ForeignFunction", "_icall$0")          \
  N(ForeignICall1,               "ForeignFunction", "_icall$1")          \
  N(ForeignICall2,               "ForeignFunction", "_icall$2")          \
  N(ForeignICall3,               "ForeignFunction", "_icall$3")          \
  N(ForeignICall4,               "ForeignFunction", "_icall$4")          \
  N(ForeignICall5,               "ForeignFunction", "_icall$5")          \
  N(ForeignICall6,               "ForeignFunction", "_icall$6")          \
                                                                         \
  N(ForeignPCall0,               "ForeignFunction", "_pcall$0")          \
  N(ForeignPCall1,               "ForeignFunction", "_pcall$1")          \
  N(ForeignPCall2,               "ForeignFunction", "_pcall$2")          \
  N(ForeignPCall3,               "ForeignFunction", "_pcall$3")          \
  N(ForeignPCall4,               "ForeignFunction", "_pcall$4")          \
  N(ForeignPCall5,               "ForeignFunction", "_pcall$5")          \
  N(ForeignPCall6,               "ForeignFunction", "_pcall$6")          \
                                                                         \
  N(ForeignVCall0,               "ForeignFunction", "_vcall$0")          \
  N(ForeignVCall1,               "ForeignFunction", "_vcall$1")          \
  N(ForeignVCall2,               "ForeignFunction", "_vcall$2")          \
  N(ForeignVCall3,               "ForeignFunction", "_vcall$3")          \
  N(ForeignVCall4,               "ForeignFunction", "_vcall$4")          \
  N(ForeignVCall5,               "ForeignFunction", "_vcall$5")          \
  N(ForeignVCall6,               "ForeignFunction", "_vcall$6")          \
                                                                         \
  N(ForeignLCallwLw,             "ForeignFunction", "_Lcall$wLw")        \
                                                                         \
  N(ForeignMarkForFinalization,  "ForeignMemory", "_markForFinalization")\
  N(ForeignAllocate,             "ForeignMemory", "_allocate")           \
  N(ForeignFree,                 "ForeignMemory", "_free")               \
                                                                         \
  N(ForeignGetInt8,              "ForeignMemory", "_getInt8")            \
  N(ForeignGetInt16,             "ForeignMemory", "_getInt16")           \
  N(ForeignGetInt32,             "ForeignMemory", "_getInt32")           \
  N(ForeignGetInt64,             "ForeignMemory", "_getInt64")           \
                                                                         \
  N(ForeignSetInt8,              "ForeignMemory", "_setInt8")            \
  N(ForeignSetInt16,             "ForeignMemory", "_setInt16")           \
  N(ForeignSetInt32,             "ForeignMemory", "_setInt32")           \
  N(ForeignSetInt64,             "ForeignMemory", "_setInt64")           \
                                                                         \
  N(ForeignGetUint8,             "ForeignMemory", "_getUint8")           \
  N(ForeignGetUint16,            "ForeignMemory", "_getUint16")          \
  N(ForeignGetUint32,            "ForeignMemory", "_getUint32")          \
  N(ForeignGetUint64,            "ForeignMemory", "_getUint64")          \
                                                                         \
  N(ForeignSetUint8,             "ForeignMemory", "_setUint8")           \
  N(ForeignSetUint16,            "ForeignMemory", "_setUint16")          \
  N(ForeignSetUint32,            "ForeignMemory", "_setUint32")          \
  N(ForeignSetUint64,            "ForeignMemory", "_setUint64")          \
                                                                         \
  N(ForeignGetFloat32,           "ForeignMemory", "_getFloat32")         \
  N(ForeignGetFloat64,           "ForeignMemory", "_getFloat64")         \
                                                                         \
  N(ForeignSetFloat32,           "ForeignMemory", "_setFloat32")         \
  N(ForeignSetFloat64,           "ForeignMemory", "_setFloat64")         \
                                                                         \
  N(StringAdd,                   "_StringImpl", "+")                     \
  N(StringCodeUnitAt,            "_StringImpl", "codeUnitAt")            \
  N(StringCreate,                "_StringImpl", "_create")               \
  N(StringEqual,                 "_StringImpl", "==")                    \
  N(StringLength,                "_StringImpl", "length")                \
  N(StringSetCodeUnitAt,         "_StringImpl", "_setCodeUnitAt")        \
  N(StringSubstring,             "_StringImpl", "_substring")            \
                                                                         \
  N(UriBase,                     "Uri", "_base")                         \
                                                                         \
  N(PortCreate,                  "Port", "_create")                      \
  N(PortSend,                    "Port", "send")                         \
  N(PortSendList,                "Port", "_sendList")                    \
  N(PortSendExit,                "Port", "_sendExit")                    \
                                                                         \
  N(SystemGetEventHandler,       "System", "_getEventHandler")           \
  N(SystemIncrementPortRef,      "System", "_incrementPortRef")          \
                                                                         \
  N(ServiceRegister,             "<none>", "register")                   \
                                                                         \
  N(IsImmutable,                 "<none>", "_isImmutable")               \
  N(IdentityHashCode,            "<none>", "_identityHashCode")          \

enum Native {
#define N(e, c, n) k##e,
NATIVES_DO(N)
#undef N
  kNumberOfNatives
};

}  // namespace fletch

#endif  // SRC_SHARED_NATIVES_H_
