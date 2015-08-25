// Copyright (c) 2015, the Fletch project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library servicec.compiler;

import 'dart:async' show
    Future;

import 'dart:io';

import 'errors.dart' show
    CompilerError;

import 'targets.dart' show
    Target;

import 'package:compiler/src/scanner/scannerlib.dart' show
    Token,
    StringScanner,
    Scanner;

import 'parser.dart' show
    Parser;

import 'listener.dart' show
    DebugListener,
    Listener;

// Temporary output type
Future<List<CompilerError>> compile(
    String path,
    String outputDirectory,
    {Target target: Target.ALL}) async {
  String input = new File(path).readAsStringSync();
  return compileInput(input, path, outputDirectory, target: target);
}

// Temporary output type
Future<List<CompilerError>> compileInput(
    String input,
    String path,
    String outputDirectory,
    {Target target: Target.ALL}) async {
  if (input.isEmpty) {
    return [CompilerError.undefinedService];
  }

  Scanner scanner = new StringScanner.fromString(input);
  Token tokens = scanner.tokenize();

  Listener listener = new Listener();
  var parser = new Parser(new DebugListener(listener));
  Token unit = parser.parseUnit(tokens);

  // TODO(stanm): validate

  // TODO(stanm): generate output

  createDirectories(outputDirectory, target);

  // TODO(stanm): write files

  return listener.errors;
}

void createDirectories(String outputDirectory, Target target) {
  new Directory(outputDirectory).createSync(recursive: true);

  if (target.includes(Target.JAVA)) {
    createJavaDirectories(outputDirectory);
  }
  if (target.includes(Target.CC)) {
    createCCDirectories(outputDirectory);
  }
}

void createJavaDirectories(String outputDirectory) {
  new Directory("$outputDirectory/java").createSync(recursive: true);
}

void createCCDirectories(String outputDirectory) {
  new Directory("$outputDirectory/cc").createSync(recursive: true);
}
