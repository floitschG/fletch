// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#ifndef SRC_SHARED_CONNECTION_H_
#define SRC_SHARED_CONNECTION_H_

#ifndef FLETCH_ENABLE_LIVE_CODING
#error "connection.h is only available when live coding is enabled."
#endif

#include "src/shared/globals.h"
#include "src/shared/platform.h"

namespace fletch {

class Socket;

class Buffer {
 public:
  Buffer();
  ~Buffer();

  void ClearBuffer();
  void SetBuffer(uint8* buffer, int length);

  int offset() const { return buffer_offset_; }

 protected:
  uint8* buffer_;
  int buffer_offset_;
  int buffer_length_;
};

class ReadBuffer : public Buffer {
 public:
  int ReadInt();
  int64 ReadInt64();
  double ReadDouble();
  bool ReadBoolean();
  uint8* ReadBytes(int* length);
};

class WriteBuffer : public Buffer {
 public:
  void EnsureCapacity(int bytes);
  void WriteInt(int value);
  void WriteInt64(int64 value);
  void WriteDouble(double value);
  void WriteBoolean(bool value);
  void WriteBytes(const uint8* bytes, int length);
  void WriteString(const char* str);
  void WriteTo(Socket* socket) const;
};

class Connection {
 public:
  enum Opcode {
    kConnectionError,
    kCompilerError,
    kSessionEnd,
    kDebugging,
    kDisableStandardOutput,
    kStdoutData,
    kStderrData,

    kProcessSpawnForMain,
    kProcessRun,
    kProcessSetBreakpoint,
    kProcessDeleteBreakpoint,
    kProcessStep,
    kProcessStepOver,
    kProcessStepOut,
    kProcessStepTo,
    kProcessContinue,
    kProcessBacktraceRequest,
    kProcessFiberBacktraceRequest,
    kProcessBacktrace,
    kProcessBreakpoint,
    kProcessLocal,
    kProcessLocalStructure,
    kProcessRestartFrame,
    kProcessTerminated,
    kProcessCompileTimeError,
    kProcessAddFibersToMap,
    kProcessNumberOfStacks,
    kWriteSnapshot,
    kCollectGarbage,

    kNewMap,
    kDeleteMap,
    kPushFromMap,
    kPopToMap,
    kRemoveFromMap,

    kDup,
    kDrop,
    kPushNull,
    kPushBoolean,
    kPushNewInteger,
    kPushNewDouble,
    kPushNewString,
    kPushNewInstance,
    kPushNewArray,
    kPushNewFunction,
    kPushNewInitializer,
    kPushNewClass,
    kPushBuiltinClass,
    kPushConstantList,
    kPushConstantByteList,
    kPushConstantMap,

    kChangeSuperClass,
    kChangeMethodTable,
    kChangeMethodLiteral,
    kChangeStatics,
    kChangeSchemas,

    kPrepareForChanges,
    kCommitChanges,
    kCommitChangesResult,
    kDiscardChanges,

    kUncaughtException,

    kMapLookup,
    kObjectId,

    kInteger,
    kBoolean,
    kNull,
    kDouble,
    kString,
    kInstance,
    kInstanceStructure
  };

  static Connection* Connect(const char* host, int port);

  virtual ~Connection();

  int ReadInt() { return incoming_.ReadInt(); }
  int64 ReadInt64() { return incoming_.ReadInt64(); }
  double ReadDouble() { return incoming_.ReadDouble(); }
  bool ReadBoolean() { return incoming_.ReadBoolean(); }
  uint8* ReadBytes(int* length) { return incoming_.ReadBytes(length); }

  void Send(Opcode opcode, const WriteBuffer& buffer);
  Opcode Receive();

 private:
  Socket* socket_;
  ReadBuffer incoming_;

  Mutex* send_mutex_;

  friend class ConnectionListener;
  Connection(const char* host, int port, Socket* socket);
};

class ConnectionListener {
 public:
  ConnectionListener(const char* host, int port);
  virtual ~ConnectionListener();

  int Port();

  Connection* Accept();

 private:
  Socket* socket_;
  int port_;
};

}  // namespace fletch

#endif  // SRC_SHARED_CONNECTION_H_
