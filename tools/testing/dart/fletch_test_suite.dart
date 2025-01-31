// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

/// Test suite for running tests in a shared Dart VM.  Take a look at
/// ../../../tests/fletch_tests/all_tests.dart for more information.
library test.fletch_test_suite;

import 'dart:io' as io;

import 'dart:convert' show
    JSON,
    LineSplitter,
    UTF8,
    Utf8Decoder;

import 'dart:async' show
    Completer,
    Future,
    Stream,
    StreamIterator,
    Timer;

import 'test_suite.dart' show
    TestSuite,
    TestUtils;

import 'test_runner.dart' show
    Command,
    CommandBuilder,
    CommandOutput,
    TestCase;

import 'runtime_configuration.dart' show
    RuntimeConfiguration;

import 'status_file_parser.dart' show
    Expectation,
    ReadTestExpectationsInto,
    TestExpectations;

import '../../../tests/fletch_tests/messages.dart' show
    ErrorMessage,
    Info,
    ListTests,
    ListTestsReply,
    Message,
    NamedMessage,
    RunTest,
    TestFailed,
    TestStdoutLine,
    TimedOut,
    messageTransformer;

class FletchTestRuntimeConfiguration extends RuntimeConfiguration {
  final String system;
  final String dartBinary;

  FletchTestRuntimeConfiguration(Map configuration)
      : system = configuration['system'],
        dartBinary = '${TestUtils.buildDir(configuration)}'
                     '${io.Platform.pathSeparator}dart',
        super.subclass();
}

class FletchTestSuite extends TestSuite {
  final String testSuiteDir;

  TestCompleter completer;

  FletchTestSuite(Map configuration, this.testSuiteDir)
      : super(configuration, "fletch_tests");

  void forEachTest(
      void onTest(TestCase testCase),
      Map testCache,
      [void onDone()]) {
    this.doTest = onTest;
    if (configuration['runtime'] != 'fletch_tests') {
      onDone();
      return;
    }

    FletchTestRuntimeConfiguration runtimeConfiguration =
        new RuntimeConfiguration(configuration);

    TestExpectations expectations = new TestExpectations();
    String buildDir = TestUtils.buildDir(configuration);

    bool helperProgramExited = false;
    io.Process vmProcess;
    ReadTestExpectationsInto(
        expectations, '$testSuiteDir/fletch_tests.status',
        configuration).then((_) {
      return io.ServerSocket.bind(io.InternetAddress.LOOPBACK_IP_V4, 0);
    }).then((io.ServerSocket server) {
      return io.Process.start(
          runtimeConfiguration.dartBinary,
          ['-Dfletch-vm=$buildDir/fletch-vm',
           '-Ddart-sdk=../dart/sdk/',
           '-Dtest.dart.build-dir=$buildDir',
           '-Dtest.dart.build-arch=${configuration["arch"]}',
           '-Dtest.dart.build-system=${configuration["system"]}',
           '-Dtest.dart.build-clang=${configuration["clang"]}',
           '-Dtest.dart.build-asan=${configuration["asan"]}',
           '-c',
           '-ppackage/',
           '-Dtest.fletch_test_suite.port=${server.port}',
           '$testSuiteDir/fletch_test_suite.dart']).then((io.Process process) {
             process.exitCode.then((_) {
               helperProgramExited = true;
               server.close();
             });
             vmProcess = process;
             return process.stdin.close();
           }).then((_) {
             return server.first.catchError((error) {
               // The VM died before we got a connection.
               assert(helperProgramExited);
               return null;
             });
           }).then((io.Socket socket) {
             server.close();
             return socket;
           });
    }).then((io.Socket socket) {
      assert(socket != null || helperProgramExited);
      completer = new TestCompleter(vmProcess, socket);
      completer.initialize();
      return completer.requestTestNames();
    }).then((List<String> testNames) {
      for (String name in testNames) {
        Set<Expectation> expectedOutcomes = expectations.expectations(name);
        TestCase testCase = new TestCase(
            'fletch_tests/$name', <Command>[], configuration, expectedOutcomes);
        var command = new FletchTestCommand(name, completer);
        testCase.commands.add(command);
        if (!expectedOutcomes.contains(Expectation.SKIP)) {
          completer.expect(command);
        }
        enqueueNewTestCase(testCase);
      }
    }).then((_) {
      onDone();
    });
  }

  void cleanup() {
    completer.allDone();
  }
}

/// Pattern that matches warnings (from dart2js) that contain a comment saying
/// "NO_LINT".
final RegExp noLintFilter =
    new RegExp(r"[^\n]*\n[^\n]*\n[^\n]* // NO_LINT\n *\^+\n");

class FletchTestOutputCommand implements CommandOutput {
  final Command command;
  final Duration time;
  final Message message;
  final List<String> stdoutLines;

  FletchTestOutputCommand(
      this.command,
      this.message,
      this.time,
      this.stdoutLines);

  Expectation result(TestCase testCase) {
    switch (message.type) {
      case 'TestPassed':
        return Expectation.PASS;

      case 'TestFailed':
        return Expectation.FAIL;

      case 'TimedOut':
        return Expectation.TIMEOUT;

      default:
        return Expectation.CRASH;
    }
  }

  bool get hasCrashed => false;

  bool get hasTimedOut => false;

  bool didFail(testCase) => message.type != 'TestPassed';

  bool hasFailed(TestCase testCase) {
    return testCase.isNegative ? !didFail(testCase) : didFail(testCase);
  }

  bool get canRunDependendCommands => false;

  bool get successful => true;

  int get exitCode => 0;

  int get pid => 0;

  List<int> get stdout {
    if (stdoutLines != null) {
      return UTF8.encode(stdoutLines.join("\n"));
    } else {
      return <int>[];
    }
  }

  List<int> get stderr {
    String result;

    switch (message.type) {
      case 'TestPassed':
      case 'TimedOut':
        return <int>[];

      case 'TestFailed':
        TestFailed failed = message;
        result = '${failed.error}\n${failed.stackTrace}';
        break;

      default:
        result = '$message';
        break;
    }
    return UTF8.encode(result);
  }

  List<String> get diagnostics => <String>[];

  bool get compilationSkipped => false;
}

class FletchTestCommand implements Command {
  final String _name;

  final TestCompleter _completer;

  FletchTestCommand(this._name, this._completer);

  String get displayName => "fletch_test";

  int get maxNumRetries => 0;

  Future<FletchTestOutputCommand> run(int timeout) {
    Stopwatch sw = new Stopwatch()..start();
    return _completer.run(this, timeout).then((NamedMessage message) {
      FletchTestOutputCommand output =
          new FletchTestOutputCommand(
              this, message, sw.elapsed, _completer.testOutput[message.name]);
      _completer.done(this);
      return output;
    });
  }

  String toString() => 'FletchTestCommand($_name)';

  set displayName(_) => throw "not supported";

  get commandLine => throw "not supported";
  set commandLine(_) => throw "not supported";

  String get reproductionCommand => throw "not supported";

  get outputIsUpToDate => throw "not supported";
}

class TestCompleter {
  final Map<String, FletchTestCommand> expected =
      new Map<String, FletchTestCommand>();
  final Map<String, Completer> completers = new Map<String, Completer>();
  final Completer<List<String>> testNamesCompleter =
      new Completer<List<String>>();
  final Map<String, List<String>> testOutput = new Map<String, List<String>>();
  final io.Process vmProcess;
  final io.Socket socket;

  int exitCode;
  String stderr = "";

  TestCompleter(this.vmProcess, this.socket);

  void initialize() {
    List<String> stderrLines = <String>[];
    Future stdoutFuture =
        vmProcess.stdout.transform(UTF8.decoder).transform(new LineSplitter())
        .listen(io.stdout.writeln).asFuture();
    bool inDartVmUncaughtMessage = false;
    Future stderrFuture =
        vmProcess.stderr.transform(UTF8.decoder).transform(new LineSplitter())
        .listen((String line) {
          io.stderr.writeln(line);
          stderrLines.add(line);
        }).asFuture();
    vmProcess.exitCode.then((value) {
      exitCode = value;
      stderr = stderrLines.join("\n");
      for (String name in completers.keys) {
        Completer completer = completers[name];
        completer.complete(
            new TestFailed(
                name,
                "Helper program exited prematurely with exit code $exitCode.",
                stderr));
      }
      if (exitCode != 0) {
        stdoutFuture.then((_) => stderrFuture).then((_) {
          throw "Helper program exited with exit code $exitCode.\n$stderr";
        });
      }
    });
    // TODO(ahe): Don't use StreamIterator here, just use listen and
    // processMessage.
    StreamIterator<Message> messages;
    if (socket == null) {
      messages = new StreamIterator<Message>(
          new Stream<Message>.fromIterable(<Message>[]));
    } else {
      messages = new StreamIterator<Message>(
          socket
          .transform(UTF8.decoder).transform(new LineSplitter())
          .transform(messageTransformer));
    }
    process(messages);
  }

  Future<List<String>> requestTestNames() {
    if (socket == null) return new Future.value(<String>[]);
    socket.writeln(JSON.encode(const ListTests().toJson()));
    return testNamesCompleter.future;
  }

  void expect(FletchTestCommand command) {
    expected[command._name] = command;
  }

  void done(FletchTestCommand command) {
    expected.remove(command._name);
    if (expected.isEmpty) {
      allDone();
    }
  }

  Future run(FletchTestCommand command, int timeout) {
    if (command._name == "self/testNeverCompletes") {
      // Ensure timeout test times out quickly.
      timeout = 1;
    }
    socket.writeln(
        JSON.encode(new RunTest(command._name).toJson()));
    Timer timer = new Timer(new Duration(seconds: timeout), () {
      socket.writeln(
          JSON.encode(new TimedOut(command._name).toJson()));
    });

    Completer completer = new Completer();
    completers[command._name] = completer;
    if (exitCode != null) {
      completer.complete(
          new TestFailed(
              command._name,
              "Helper program exited prematurely with exit code $exitCode.",
              stderr));
    }
    return completer.future.then((value) {
      timer.cancel();
      return value;
    });
  }

  void processMessage(Message message) {
    switch (message.type) {
      case 'Info':
        Info info = message;
        // For debugging, shouldn't normally be called.
        print(info.data);
        break;
      case 'TestPassed':
      case 'TestFailed':
      case 'TimedOut':
        NamedMessage namedMessage = message;
        Completer completer = completers.remove(namedMessage.name);
        completer.complete(message);
        break;
      case 'ListTestsReply':
        ListTestsReply reply = message;
        testNamesCompleter.complete(reply.tests);
        break;
      case 'InternalErrorMessage':
        ErrorMessage error = message;
        print(error.error);
        print(error.stackTrace);
        throw "Internal error in helper process: ${error.error}";
      case 'TestStdoutLine':
        TestStdoutLine line = message;
        testOutput.putIfAbsent(line.name, () => <String>[]).add(line.line);
        break;
      default:
        throw "Unhandled message from helper process: $message";
    }
  }

  void process(StreamIterator<Message> messages) {
    messages.moveNext().then((bool hasNext) {
      if (hasNext) {
        processMessage(messages.current);
        process(messages);
      }
    });
  }

  void allDone() {
    // This should cause the vmProcess to exit.
    socket.close();
  }
}
