// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

/// Modify this file to include more tests.
library fletch_tests.all_tests;

import 'dart:async' show
    Completer,
    Future;

import 'self_tests.dart' as self;

import 'verb_tests.dart' as verbs;

import '../fletchc/incremental/feature_test.dart' as incremental;

import '../fletchc/driver/test_control_stream.dart' as controlStream;

import 'zone_helper_tests.dart' as zone_helper;

import 'sentence_tests.dart' as sentence_tests;

import 'message_tests.dart' as message_tests;

import 'print_backtrace_tests.dart' as print_backtrace;

import '../service_tests/service_tests.dart' as service_tests;

import '../servicec/servicec_tests.dart' as servicec_tests;

typedef Future NoArgFuture();

/// Map of names to tests or collections of tests.
///
/// Regarding the entries of this map:
///
/// If the key does NOT end with '/*', it is considered a normal test case, and
/// the value is a closure that returns a future that completes as the test
/// completes. If the test fails, it should complete with an error.
///
/// Otherwise, if the key DOES end with '/*', it is considered a collection of
/// tests, and the value must be a closure that returns a `Future<Map<String,
/// NoArgFuture>>` consting of only normal test cases.
const Map<String, NoArgFuture> TESTS = const <String, NoArgFuture>{
  'self/testSleepForThreeSeconds': self.testSleepForThreeSeconds,
  'self/testAlwaysFails': self.testAlwaysFails,
  'self/testNeverCompletes': self.testNeverCompletes,
  'self/testMessages': self.testMessages,
  'self/testPrint': self.testPrint,

  'verbs/helpTextFormat': verbs.testHelpTextFormatCompliance,

  'incremental/*': incremental.listTests,

  'controlStream/testControlStream': controlStream.testControlStream,

  'zone_helper/testEarlySyncError': zone_helper.testEarlySyncError,
  'zone_helper/testEarlyAsyncError': zone_helper.testEarlyAsyncError,
  'zone_helper/testLateError': zone_helper.testLateError,
  'zone_helper/testUnhandledLateError': zone_helper.testUnhandledLateError,
  'zone_helper/testAlwaysFails': zone_helper.testAlwaysFails,
  'zone_helper/testCompileTimeError': zone_helper.testCompileTimeError,

  'sentence_tests': sentence_tests.main,

  'message_tests': message_tests.main,

  'print_backtrace/simulateVmCrash': print_backtrace.simulateVmCrash,
  'print_backtrace/simulateNullBacktrace':
      print_backtrace.simulateNullBacktrace,
  'print_backtrace/simulateBadBacktraceHack':
      print_backtrace.simulateBadBacktraceHack,
  'print_backtrace/simulateBadBacktrace': print_backtrace.simulateBadBacktrace,

  'service_tests/*': service_tests.listTests,

  'servicec/*': servicec_tests.listTests,
};
