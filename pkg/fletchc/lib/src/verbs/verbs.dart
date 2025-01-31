// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

library fletchc.verbs.verbs;

import 'infrastructure.dart' show
    AnalyzedSentence,
    Future,
    TargetKind,
    VerbContext;

import 'debug_verb.dart' show
    debugVerb;

import 'help_verb.dart' show
    helpVerb;

import 'show_verb.dart' show
    showVerb;

import 'compile_and_run_verb.dart' show
    compileAndRunVerb;

import 'shutdown_verb.dart' show
    shutdownVerb;

import 'create_verb.dart' show
    createVerb;

import 'compile_verb.dart' show
    compileVerb;

import 'attach_verb.dart' show
    attachVerb;

import 'run_verb.dart' show
    runVerb;

import 'x_end_verb.dart' show
    endVerb;

import 'x_servicec_verb.dart' show
    servicecVerb;

typedef Future<int> DoVerb(AnalyzedSentence sentence, VerbContext context);

class Verb {
  final DoVerb perform;

  final String documentation;

  /// True if this verb needs "in session NAME".
  final bool requiresSession;

  /// True if this verb requires a session target (that is, "session NAME"
  /// without "in").
  final bool requiresTargetSession;

  /// True if this verb allows trailing arguments.
  final bool allowsTrailing;

  /// Optional kind of target required by this verb.
  final TargetKind requiredTarget;

  /// Optional list of targets supported (but not required) by this verb.
  final List<TargetKind> supportedTargets;

  const Verb(
      this.perform,
      this.documentation,
      {this.requiresSession: false,
       this.allowsTrailing: false,
       bool requiresTargetSession: false,
       TargetKind requiredTarget,
       this.supportedTargets})
      : this.requiresTargetSession = requiresTargetSession,
        this.requiredTarget =
            requiresTargetSession ? TargetKind.SESSION : requiredTarget;
}


// TODO(ahe): Support short and long documentation.

/// Common verbs are displayed in the default help screen.
///
/// Please make sure their combined documentation fit in in 80 columns by 20
/// lines.  The default terminal size is normally 80x24.  Two lines are used
/// for the prompts before and after running fletch.  Another two lines may be
/// used to print an error message.
const Map<String, Verb> commonVerbs = const <String, Verb>{
  "help": helpVerb,
  "show": showVerb
};

/// Uncommon verbs aren't displayed in the normal help screen.
///
/// These verbs are displayed when running `fletch help all`.
const Map<String, Verb> uncommonVerbs = const <String, Verb>{
  "compile-and-run": compileAndRunVerb,
  "debug": debugVerb,
  "shutdown": shutdownVerb,
  "create": createVerb,
  "compile": compileVerb,
  "attach": attachVerb,
  "x-run": runVerb,
  "x-end": endVerb,
  "x-servicec": servicecVerb,
};
