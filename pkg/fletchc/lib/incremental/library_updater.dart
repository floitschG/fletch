// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library fletchc_incremental.library_updater;

import 'dart:async' show
    Future;

import 'package:compiler/compiler_new.dart' show
    CompilerDiagnostics,
    Diagnostic;

import 'package:compiler/compiler.dart' as api;

import 'package:compiler/src/dart2jslib.dart' show
    Compiler,
    EnqueueTask,
    MessageKind,
    Script;

import 'package:compiler/src/elements/elements.dart' show
    AstElement,
    ClassElement,
    CompilationUnitElement,
    Element,
    FieldElement,
    FunctionElement,
    LibraryElement,
    STATE_NOT_STARTED,
    ScopeContainerElement,
    TypeDeclarationElement;

import 'package:compiler/src/scanner/scannerlib.dart' show
    EOF_TOKEN,
    Listener,
    NodeListener,
    Parser,
    PartialClassElement,
    PartialElement,
    PartialFieldList,
    PartialFunctionElement,
    Scanner,
    Token;

import 'package:compiler/src/io/source_file.dart' show
    CachingUtf8BytesSourceFile,
    SourceFile,
    StringSourceFile;

import 'package:compiler/src/tree/tree.dart' show
    ClassNode,
    FunctionExpression,
    LibraryTag,
    NodeList,
    Part,
    StringNode,
    unparse;

import '../src/fletch_backend.dart' show
    FletchBackend;

import '../src/fletch_class_builder.dart' show
    FletchClassBuilder;

import '../src/fletch_context.dart' show
    FletchContext;

import '../commands.dart' show
    Command,
    MapId;

import '../commands.dart' as commands_lib;

import '../fletch_system.dart';

import 'package:compiler/src/util/util.dart' show
    Link,
    LinkBuilder;

import 'package:compiler/src/elements/modelx.dart' show
    ClassElementX,
    CompilationUnitElementX,
    DeclarationSite,
    ElementX,
    FieldElementX,
    LibraryElementX;

import 'package:compiler/src/universe/universe.dart' show
    Selector,
    UniverseSelector;

import 'package:compiler/src/constants/values.dart' show
    ConstantValue;

import 'package:compiler/src/library_loader.dart' show
    TagState;

import 'diff.dart' show
    Difference,
    computeDifference;

import 'fletchc_incremental.dart' show
    IncrementalCompilationFailed,
    IncrementalCompiler;

import '../src/fletch_function_builder.dart' show
    FletchFunctionBuilder;

typedef void Logger(message);

typedef bool Reuser(
    Token diffToken,
    PartialElement before,
    PartialElement after);

class FailedUpdate {
  /// Either an [Element] or a [Difference].
  final context;
  final String message;

  FailedUpdate(this.context, this.message);

  String toString() {
    if (context == null) return '$message';
    return 'In $context:\n  $message';
  }
}

abstract class _IncrementalCompilerContext {
  IncrementalCompiler incrementalCompiler;

  Set<ClassElementX> _emittedClasses;

  Set<ClassElementX> _directlyInstantiatedClasses;

  Set<ConstantValue> _compiledConstants;
}

class IncrementalCompilerContext extends _IncrementalCompilerContext
    implements CompilerDiagnostics {
  final CompilerDiagnostics diagnostics;

  final Set<Uri> _uriWithUpdates = new Set<Uri>();

  IncrementalCompilerContext(this.diagnostics);

  void set incrementalCompiler(IncrementalCompiler value) {
    if (super.incrementalCompiler != null) {
      throw new StateError("Can't set [incrementalCompiler] more than once");
    }
    super.incrementalCompiler = value;
  }

  void registerUriWithUpdates(Iterable<Uri> uris) {
    _uriWithUpdates.addAll(uris);
  }

  void _captureState(Compiler compiler) {
    // TODO(ahe): Compute this.
    _emittedClasses = new Set();

    _directlyInstantiatedClasses =
        new Set.from(compiler.codegenWorld.directlyInstantiatedClasses);

    // TODO(ahe): Compute this.
    List<ConstantValue> constants = [];
    if (constants == null) constants = <ConstantValue>[];
    _compiledConstants = new Set<ConstantValue>.identity()..addAll(constants);
  }

  bool _uriHasUpdate(Uri uri) => _uriWithUpdates.contains(uri);

  void report(
      var code,
      Uri uri,
      int begin,
      int end,
      String text,
      Diagnostic kind) {
    if (_uriHasUpdate(uri)) {
      // TODO(ahe): Map location to updated source file.
      print("$uri+$begin-$end: $text");
    } else {
      diagnostics.report(code, uri, begin, end, text, kind);
    }
  }
}

class LibraryUpdater extends FletchFeatures {
  final Compiler compiler;

  final api.CompilerInputProvider inputProvider;

  final Logger logTime;

  final Logger logVerbose;

  final List<Update> updates = <Update>[];

  final List<FailedUpdate> _failedUpdates = <FailedUpdate>[];

  final Set<ElementX> _elementsToInvalidate = new Set<ElementX>();

  final Set<ElementX> _removedElements = new Set<ElementX>();

  final IncrementalCompilerContext _context;

  final Map<Uri, Future> _sources = <Uri, Future>{};

  /// Cached tokens of entry compilation units.
  final Map<LibraryElementX, Token> _entryUnitTokens =
      <LibraryElementX, Token>{};

  /// Cached source files for entry compilation units.
  final Map<LibraryElementX, SourceFile> _entrySourceFiles =
      <LibraryElementX, SourceFile>{};

  bool _hasCapturedCompilerState = false;

  LibraryUpdater(
      this.compiler,
      this.inputProvider,
      this.logTime,
      this.logVerbose,
      this._context) {
    // TODO(ahe): Would like to remove this from the constructor. However, the
    // state must be captured before calling [reuseCompiler].
    // Proper solution might be: [reuseCompiler] should not clear the sets that
    // are captured in [IncrementalCompilerContext._captureState].
    _ensureCompilerStateCaptured();
  }

  /// Returns the classes emitted by [compiler].
  Set<ClassElementX> get _emittedClasses => _context._emittedClasses;

  /// Returns the directly instantantiated classes seen by [compiler] (this
  /// includes interfaces and may be different from [_emittedClasses] that only
  /// includes interfaces used in type tests).
  Set<ClassElementX> get _directlyInstantiatedClasses {
    return _context._directlyInstantiatedClasses;
  }

  /// Returns the constants emitted by [compiler].
  Set<ConstantValue> get _compiledConstants => _context._compiledConstants;

  /// When [true], updates must be applied (using [applyUpdates]) before the
  /// [compiler]'s state correctly reflects the updated program.
  bool get hasPendingUpdates => !updates.isEmpty;

  bool get failed => !_failedUpdates.isEmpty;

  /// Used as tear-off passed to [LibraryLoaderTask.resetAsync].
  Future<bool> reuseLibrary(LibraryElement library) {
    _ensureCompilerStateCaptured();
    assert(compiler != null);
    if (library.isPlatformLibrary) {
      logTime('Reusing $library (assumed read-only).');
      return new Future.value(true);
    }
    return _haveTagsChanged(library).then((bool haveTagsChanged) {
      if (haveTagsChanged) {
        cannotReuse(
            library,
            "Changes to library, import, export, or part declarations not"
            " supported.");
        return true;
      }

      bool isChanged = false;
      List<Future<Script>> futureScripts = <Future<Script>>[];

      for (CompilationUnitElementX unit in library.compilationUnits) {
        Uri uri = unit.script.resourceUri;
        if (_context._uriHasUpdate(uri)) {
          isChanged = true;
          futureScripts.add(_updatedScript(unit.script, library));
        } else {
          futureScripts.add(new Future.value(unit.script));
        }
      }

      if (!isChanged) {
        logTime("Reusing $library, source didn't change.");
        return true;
      }

      return Future.wait(futureScripts).then(
          (List<Script> scripts) => canReuseLibrary(library, scripts));
    }).whenComplete(() => _cleanUp(library));
  }

  void _cleanUp(LibraryElementX library) {
    _entryUnitTokens.remove(library);
    _entrySourceFiles.remove(library);
  }

  Future<Script> _updatedScript(Script before, LibraryElementX library) {
    if (before == library.entryCompilationUnit.script &&
        _entrySourceFiles.containsKey(library)) {
      return new Future.value(before.copyWithFile(_entrySourceFiles[library]));
    }

    return _readUri(before.resourceUri).then((bytes) {
      Uri uri = before.file.uri;
      String filename = before.file.filename;
      SourceFile sourceFile = bytes is String
          ? new StringSourceFile(uri, filename, bytes)
          : new CachingUtf8BytesSourceFile(uri, filename, bytes);
      return before.copyWithFile(sourceFile);
    });
  }

  Future<bool> _haveTagsChanged(LibraryElement library) {
    Script before = library.entryCompilationUnit.script;
    if (!_context._uriHasUpdate(before.resourceUri)) {
      // The entry compilation unit hasn't been updated. So the tags aren't
      // changed.
      return new Future<bool>.value(false);
    }

    return _updatedScript(before, library).then((Script script) {
      _entrySourceFiles[library] = script.file;
      Token token = new Scanner(_entrySourceFiles[library]).tokenize();
      _entryUnitTokens[library] = token;
      // Using two parsers to only create the nodes we want ([LibraryTag]).
      Parser parser = new Parser(new Listener());
      NodeListener listener = new NodeListener(
          compiler, library.entryCompilationUnit);
      Parser nodeParser = new Parser(listener);
      Iterator<LibraryTag> tags = library.tags.iterator;
      while (token.kind != EOF_TOKEN) {
        token = parser.parseMetadataStar(token);
        if (parser.optional('library', token) ||
            parser.optional('import', token) ||
            parser.optional('export', token) ||
            parser.optional('part', token)) {
          if (!tags.moveNext()) return true;
          token = nodeParser.parseTopLevelDeclaration(token);
          LibraryTag tag = listener.popNode();
          assert(listener.nodes.isEmpty);
          if (unparse(tags.current) != unparse(tag)) {
            return true;
          }
        } else {
          break;
        }
      }
      return tags.moveNext();
    });
  }

  Future _readUri(Uri uri) {
    return _sources.putIfAbsent(uri, () => inputProvider(uri));
  }

  void _ensureCompilerStateCaptured() {
    // TODO(ahe): [compiler] shouldn't be null, remove the following line.
    if (compiler == null) return;

    if (_hasCapturedCompilerState) return;
    _context._captureState(compiler);
    _hasCapturedCompilerState = true;
  }

  /// Returns true if [library] can be reused.
  ///
  /// This methods also computes the [updates] (patches) needed to have
  /// [library] reflect the modifications in [scripts].
  bool canReuseLibrary(LibraryElement library, List<Script> scripts) {
    logTime('Attempting to reuse ${library}.');

    Uri entryUri = library.entryCompilationUnit.script.resourceUri;
    Script entryScript =
        scripts.singleWhere((Script script) => script.resourceUri == entryUri);
    LibraryElementX newLibrary =
        new LibraryElementX(entryScript, library.canonicalUri);
    if (_entryUnitTokens.containsKey(library)) {
      compiler.dietParser.dietParse(
          newLibrary.entryCompilationUnit, _entryUnitTokens[library]);
    } else {
      compiler.scanner.scanLibrary(newLibrary);
    }

    TagState tagState = new TagState();
    for (LibraryTag tag in newLibrary.tags) {
      if (tag.isImport) {
        tagState.checkTag(TagState.IMPORT_OR_EXPORT, tag, compiler);
      } else if (tag.isExport) {
        tagState.checkTag(TagState.IMPORT_OR_EXPORT, tag, compiler);
      } else if (tag.isLibraryName) {
        tagState.checkTag(TagState.LIBRARY, tag, compiler);
        if (newLibrary.libraryTag == null) {
          // Use the first if there are multiple (which is reported as an
          // error in [TagState.checkTag]).
          newLibrary.libraryTag = tag;
        }
      } else if (tag.isPart) {
        tagState.checkTag(TagState.PART, tag, compiler);
      }
    }

    // TODO(ahe): Process tags using TagState, not
    // LibraryLoaderTask.processLibraryTags.
    Link<CompilationUnitElement> units = library.compilationUnits;
    for (Script script in scripts) {
      CompilationUnitElementX unit = units.head;
      units = units.tail;
      if (script != entryScript) {
        // TODO(ahe): Copied from library_loader.
        CompilationUnitElement newUnit =
            new CompilationUnitElementX(script, newLibrary);
        compiler.withCurrentElement(newUnit, () {
          compiler.scanner.scan(newUnit);
          if (unit.partTag == null) {
            compiler.reportError(unit, MessageKind.MISSING_PART_OF_TAG);
          }
        });
      }
    }

    logTime('New library synthesized.');
    return canReuseScopeContainerElement(library, newLibrary);
  }

  bool cannotReuse(context, String message) {
    _failedUpdates.add(new FailedUpdate(context, message));
    logVerbose(message);
    return false;
  }

  bool canReuseScopeContainerElement(
      ScopeContainerElement element,
      ScopeContainerElement newElement) {
    if (checkForGenericTypes(element)) return false;
    if (checkForGenericTypes(newElement)) return false;
    List<Difference> differences = computeDifference(element, newElement);
    logTime('Differences computed.');
    for (Difference difference in differences) {
      logTime('Looking at difference: $difference');

      if (difference.before == null && difference.after is PartialElement) {
        canReuseAddedElement(difference.after, element, newElement);
        continue;
      }
      if (difference.after == null && difference.before is PartialElement) {
        canReuseRemovedElement(difference.before, element);
        continue;
      }
      Token diffToken = difference.token;
      if (diffToken == null) {
        cannotReuse(difference, "No difference token.");
        continue;
      }
      if (difference.after is! PartialElement &&
          difference.before is! PartialElement) {
        cannotReuse(difference, "Don't know how to recompile.");
        continue;
      }
      PartialElement before = difference.before;
      PartialElement after = difference.after;

      Reuser reuser;

      if (before is PartialFunctionElement && after is PartialFunctionElement) {
        reuser = canReuseFunction;
      } else if (before is PartialClassElement &&
                 after is PartialClassElement) {
        reuser = canReuseClass;
      } else {
        reuser = unableToReuse;
      }
      if (!reuser(diffToken, before, after)) {
        assert(!_failedUpdates.isEmpty);
        continue;
      }
    }

    return _failedUpdates.isEmpty;
  }

  bool canReuseAddedElement(
      PartialElement element,
      ScopeContainerElement container,
      ScopeContainerElement syntheticContainer) {
    if (element is PartialFunctionElement) {
      addFunction(element, container);
      return true;
    } else if (element is PartialClassElement) {
      addClass(element, container);
      return true;
    } else if (element is PartialFieldList) {
      addFields(element, container, syntheticContainer);
      return true;
    }
    return cannotReuse(element, "Adding ${element.runtimeType} not supported.");
  }

  void addFunction(
      PartialFunctionElement element,
      /* ScopeContainerElement */ container) {
    invalidateScopesAffectedBy(element, container);

    updates.add(new AddedFunctionUpdate(compiler, element, container));
  }

  void addClass(
      PartialClassElement element,
      LibraryElementX library) {
    invalidateScopesAffectedBy(element, library);

    updates.add(new AddedClassUpdate(compiler, element, library));
  }

  /// Called when a field in [definition] has changed.
  ///
  /// There's no direct link from a [PartialFieldList] to its implied
  /// [FieldElementX], so instead we use [syntheticContainer], the (synthetic)
  /// container created by [canReuseLibrary], or [canReuseClass] (through
  /// [PartialClassElement.parseNode]). This container is scanned looking for
  /// fields whose declaration site is [definition].
  // TODO(ahe): It would be nice if [computeDifference] returned this
  // information directly.
  void addFields(
      PartialFieldList definition,
      ScopeContainerElement container,
      ScopeContainerElement syntheticContainer) {
    List<FieldElementX> fields = <FieldElementX>[];
    syntheticContainer.forEachLocalMember((ElementX member) {
      if (member.declarationSite == definition) {
        fields.add(member);
      }
    });
    for (FieldElementX field in fields) {
      // TODO(ahe): This only works when there's one field per
      // PartialFieldList.
      addField(field, container);
    }
  }

  void addField(FieldElementX element, ScopeContainerElement container) {
    logVerbose("Add field $element to $container.");
    invalidateScopesAffectedBy(element, container);
    updates.add(new AddedFieldUpdate(compiler, element, container));
  }

  bool canReuseRemovedElement(
      PartialElement element,
      ScopeContainerElement container) {
    if (element is PartialFunctionElement) {
      removeFunction(element);
      return true;
    } else if (element is PartialClassElement) {
      removeClass(element);
      return true;
    } else if (element is PartialFieldList) {
      removeFields(element, container);
      return true;
    }
    return cannotReuse(
        element, "Removing ${element.runtimeType} not supported.");
  }

  void removeFunction(PartialFunctionElement element) {
    logVerbose("Removed method $element.");

    invalidateScopesAffectedBy(element, element.enclosingElement);

    _removedElements.add(element);

    updates.add(new RemovedFunctionUpdate(compiler, element));
  }

  void removeClass(PartialClassElement element) {
    logVerbose("Removed class $element.");

    invalidateScopesAffectedBy(element, element.library);

    _removedElements.add(element);
    element.forEachLocalMember((ElementX member) {
      _removedElements.add(member);
    });

    updates.add(new RemovedClassUpdate(compiler, element));
  }

  void removeFields(
      PartialFieldList definition,
      ScopeContainerElement container) {
    List<FieldElementX> fields = <FieldElementX>[];
    container.forEachLocalMember((ElementX member) {
      if (member.declarationSite == definition) {
        fields.add(member);
      }
    });
    for (FieldElementX field in fields) {
      // TODO(ahe): This only works when there's one field per
      // PartialFieldList.
      removeField(field);
    }
  }

  void removeField(FieldElementX element) {
    logVerbose("Removed field $element.");
    if (!element.isInstanceMember) {
      cannotReuse(element, "Not an instance field.");
    } else {
      removeInstanceField(element);
    }
  }

  void removeInstanceField(FieldElementX element) {
    PartialClassElement cls = element.enclosingClass;

    invalidateScopesAffectedBy(element, cls);

    _removedElements.add(element);

    updates.add(new RemovedFieldUpdate(compiler, element));
  }

  /// Returns true if [element] has generic types (or if we cannot rule out
  /// that it has generic types).
  bool checkForGenericTypes(Element element) {
    if (element is TypeDeclarationElement) {
      if (!element.isResolved) {
        if (element is PartialClassElement) {
          ClassNode node = element.parseNode(compiler).asClassNode();
          if (node == null) {
            cannotReuse(
                element, "Class body isn't a ClassNode on $element");
            return true;
          }
          bool isGeneric =
              node.typeParameters != null && !node.typeParameters.isEmpty;
          if (isGeneric) {
            // TODO(ahe): Support generic types.
            cannotReuse(
                element,
                "Type variables not supported: '${node.typeParameters}'");
            return true;
          }
        } else {
          cannotReuse(
              element, "Can't check for generic types on $element");
          return true;
        }
      } else if (!element.thisType.isRaw) {
        cannotReuse(
            element, "Generic types not supported: '${element.thisType}'");
        return true;
      }
    }
    return false;
  }

  void invalidateScopesAffectedBy(
      ElementX element,
      /* ScopeContainerElement */ container) {
    if (checkForGenericTypes(element)) return;
    for (ScopeContainerElement scope in scopesAffectedBy(element, container)) {
      scanSites(scope, (Element member, DeclarationSite site) {
        // TODO(ahe): Cache qualifiedNamesIn to avoid quadratic behavior.
        Set<String> names = qualifiedNamesIn(site);
        if (canNamesResolveStaticallyTo(names, element, container)) {
          if (checkForGenericTypes(member)) return;
          if (member is TypeDeclarationElement) {
            if (!member.isResolved) {
              // TODO(ahe): This is a bug in dart2js' forgetElement which
              // attempts to check if member is a generic type.
              cannotReuse(member, "Not resolved");
              return;
            }
          }
          _elementsToInvalidate.add(member);
        }
      });
    }
  }

  void replaceFunctionInBackend(
      ElementX element,
      /* ScopeContainerElement */ container) {
    List<Element> elements = <Element>[];
    if (checkForGenericTypes(element)) return;
    for (ScopeContainerElement scope in scopesAffectedBy(element, container)) {
      scanSites(scope, (Element member, DeclarationSite site) {
        // TODO(ahe): Cache qualifiedNamesIn to avoid quadratic behavior.
        Set<String> names = qualifiedNamesIn(site);
        if (canNamesResolveStaticallyTo(names, element, container)) {
          if (checkForGenericTypes(member)) return;
          if (member is TypeDeclarationElement) {
            if (!member.isResolved) {
              // TODO(ahe): This is a bug in dart2js' forgetElement which
              // attempts to check if member is a generic type.
              cannotReuse(member, "Not resolved");
              return;
            }
          }
          elements.add(member);
        }
      });
    }
    backend.replaceFunctionUsageElement(element, elements);
  }

  /// Invoke [f] on each [DeclarationSite] in [element]. If [element] is a
  /// [ScopeContainerElement], invoke f on all local members as well.
  void scanSites(
      Element element,
      void f(ElementX element, DeclarationSite site)) {
    DeclarationSite site = declarationSite(element);
    if (site != null) {
      f(element, site);
    }
    if (element is ScopeContainerElement) {
      element.forEachLocalMember((member) { scanSites(member, f); });
    }
  }

  /// Assume [element] is either removed from or added to [container], and
  /// return all [ScopeContainerElement] that can see this change.
  List<ScopeContainerElement> scopesAffectedBy(
      Element element,
      /* ScopeContainerElement */ container) {
    // TODO(ahe): Use library export graph to compute this.
    // TODO(ahe): Should return all user-defined libraries and packages.
    LibraryElement library = container.library;
    List<ScopeContainerElement> result = <ScopeContainerElement>[library];

    if (!container.isClass) return result;

    ClassElement cls = container;

    if (!compiler.world.isInstantiated(cls.declaration)) {
      // dart2js currently only maintain subclasses for classes that are
      // instantiated.
      throw new IncrementalCompilationFailed(
          "Unable to compute subclasses of ${cls.declaration}");
    }
    var externalSubtypes =
        compiler.world.subclassesOf(cls).where((e) => e.library != library);

    return result..addAll(externalSubtypes);
  }

  /// Returns true if function [before] can be reused to reflect the changes in
  /// [after].
  ///
  /// If [before] can be reused, an update (patch) is added to [updates].
  bool canReuseFunction(
      Token diffToken,
      PartialFunctionElement before,
      PartialFunctionElement after) {
    FunctionExpression node =
        after.parseNode(compiler).asFunctionExpression();
    if (node == null) {
      return cannotReuse(after, "Not a function expression: '$node'");
    }
    Token last = after.endToken;
    if (node.body != null) {
      last = node.body.getBeginToken();
    }
    if (before.isErroneous ||
        isTokenBetween(diffToken, after.beginToken, last)) {
      removeFunction(before);
      addFunction(after, before.enclosingElement);
      if (compiler.mainFunction == before) {
        return cannotReuse(
            after,
            "Unable to handle when signature of '${after.name}' changes");
      }
      return true;
    }
    logVerbose('Simple modification of ${after} detected');
    updates.add(new FunctionUpdate(compiler, before, after));
    return true;
  }

  bool canReuseClass(
      Token diffToken,
      PartialClassElement before,
      PartialClassElement after) {
    ClassNode node = after.parseNode(compiler).asClassNode();
    if (node == null) {
      return cannotReuse(after, "Not a ClassNode: '$node'");
    }
    NodeList body = node.body;
    if (body == null) {
      return cannotReuse(after, "Class has no body.");
    }
    if (isTokenBetween(diffToken, node.beginToken, body.beginToken.next)) {
      logVerbose('Class header modified in ${after}');
      updates.add(new ClassUpdate(compiler, before, after));
      before.forEachLocalMember((ElementX member) {
        // TODO(ahe): Quadratic.
        invalidateScopesAffectedBy(member, before);
      });
    } else {
      logVerbose('Simple modification of ${after} detected');
    }
    return canReuseScopeContainerElement(before, after);
  }

  /// Returns true if [token] is found between [first] (included) and [last]
  /// (excluded).
  bool isTokenBetween(Token token, Token first, Token last) {
    Token current = first;
    while (current != last && current.kind != EOF_TOKEN) {
      if (current == token) {
        return true;
      }
      current = current.next;
    }
    return false;
  }

  bool unableToReuse(
      Token diffToken,
      PartialElement before,
      PartialElement after) {
    return cannotReuse(
        after,
        'Unhandled change:'
        ' ${before} (${before.runtimeType} -> ${after.runtimeType}).');
  }

  /// Apply the collected [updates]. Return a list of elements that needs to be
  /// recompiled after applying the updates.
  List<Element> applyUpdates() {
    for (Update update in updates) {
      update.captureState();
    }
    if (!_failedUpdates.isEmpty) {
      throw new IncrementalCompilationFailed(_failedUpdates.join('\n\n'));
    }
    for (ElementX element in _elementsToInvalidate) {
      compiler.forgetElement(element);
      element.reuseElement();
    }
    List<Element> elementsToInvalidate = <Element>[];
    for (ElementX element in _elementsToInvalidate) {
      if (!_removedElements.contains(element)) {
        elementsToInvalidate.add(element);
      }
    }
    for (Update update in updates) {
      Element element = update.apply();
      if (!update.isRemoval) {
        elementsToInvalidate.add(element);
      }
      if (update is FunctionUpdate) {
        replaceFunctionInBackend(element, element.enclosingElement);
      }
    }
    return elementsToInvalidate;
  }

  static void forEachField(ClassElement c, void action(FieldElement field)) {
    List classes = [];
    while (c != null) {
      if (!c.isResolved) {
        throw new IncrementalCompilationFailed("Class not resolved: $c");
      }
      classes.add(c);
      c = c.superclass;
    }
    for (int i = classes.length - 1; i >= 0; i--) {
      classes[i].implementation.forEachInstanceField((_, FieldElement field) {
        action(field);
      });
    }
  }

  FletchDelta computeUpdateFletch(FletchSystem currentSystem) {
    // TODO(ahe): Remove this when we support adding static fields.
    Set<Element> existingStaticFields =
        new Set<Element>.from(fletchContext.staticIndices.keys);

    backend.newSystemBuilder(currentSystem);

    List<Element> updatedElements = applyUpdates();

    if (compiler.progress != null) {
      compiler.progress.reset();
    }

    for (Element element in updatedElements) {
      if (!element.isClass) {
        enqueuer.resolution.addToWorkList(element);
      } else {
        NO_WARN(element).ensureResolved(compiler);
      }
    }
    compiler.processQueue(enqueuer.resolution, null);

    compiler.phase = Compiler.PHASE_DONE_RESOLVING;

    // TODO(ahe): Clean this up. Don't call this method in analyze-only mode.
    if (compiler.analyzeOnly) {
      return new FletchDelta(currentSystem, currentSystem, <Command>[]);
    }

    for (AstElement element in updatedElements) {
      if (element.node.isErroneous) {
        throw new IncrementalCompilationFailed(
            "Unable to incrementally compile $element with syntax error");
      }
      if (element.isField) {
        backend.newElement(element);
      } else if (!element.isClass) {
        enqueuer.codegen.addToWorkList(element);
      }
    }
    compiler.processQueue(enqueuer.codegen, null);

    // TODO(ahe): Remove this when we support adding static fields.
    Set<Element> newStaticFields =
        new Set<Element>.from(fletchContext.staticIndices.keys).difference(
            existingStaticFields);
    if (!newStaticFields.isEmpty) {
      throw new IncrementalCompilationFailed(
          "Unable to add static fields:\n  ${newStaticFields.join(',\n  ')}");
    }

    // Run through all compiled methods and see if they may apply to
    // newlySeenSelectors.
    for (Element e in enqueuer.codegen.generatedCode.keys) {
      if (e.isFunction && !e.isConstructor &&
          (e as dynamic).functionSignature.hasOptionalParameters) {
        for (UniverseSelector selector in enqueuer.codegen.newlySeenSelectors) {
          // TODO(ahe): Group selectors by name at this point for improved
          // performance.
          if (e.isInstanceMember &&
              selector.selector.applies(e, compiler.world)) {
            // TODO(ahe): Don't use
            // enqueuer.codegen.newlyEnqueuedElements directly like
            // this, make a copy.
            enqueuer.codegen.newlyEnqueuedElements.add(e);
          }
        }
      }
    }

    List<Command> commands = <Command>[const commands_lib.PrepareForChanges()];
    FletchSystem system = backend.systemBuilder.computeSystem(
        backend.context,
        commands);
    return new FletchDelta(system, currentSystem, commands);
  }
}

/// Represents an update (aka patch) of [before] to [after]. We use the word
/// "update" to avoid confusion with the compiler feature of "patch" methods.
abstract class Update {
  final Compiler compiler;

  PartialElement get before;

  PartialElement get after;

  Update(this.compiler);

  /// Applies the update to [before] and returns that element.
  Element apply();

  bool get isRemoval => false;

  /// Called before any patches are applied to capture any state that is needed
  /// later.
  void captureState() {
  }
}

/// Represents an update of a function element.
class FunctionUpdate extends Update with ReuseFunction {
  final PartialFunctionElement before;

  final PartialFunctionElement after;

  FunctionUpdate(Compiler compiler, this.before, this.after)
      : super(compiler);

  PartialFunctionElement apply() {
    patchElement();
    reuseElement();
    return before;
  }

  /// Destructively change the tokens in [before] to match those of [after].
  void patchElement() {
    before.beginToken = after.beginToken;
    before.endToken = after.endToken;
    before.getOrSet = after.getOrSet;
  }
}

abstract class ReuseFunction {
  Compiler get compiler;

  PartialFunctionElement get before;

  /// Reset various caches and remove this element from the compiler's internal
  /// state.
  void reuseElement() {
    compiler.forgetElement(before);
    before.reuseElement();
  }
}

abstract class RemovalUpdate extends Update {
  ElementX get element;

  RemovalUpdate(Compiler compiler)
      : super(compiler);

  bool get isRemoval => true;

  void writeUpdateFletchOn(List<Command> updates);

  void removeFromEnclosing() {
    // TODO(ahe): Need to recompute duplicated elements logic again. Simplest
    // solution is probably to remove all elements from enclosing scope and add
    // them back.
    if (element.isTopLevel) {
      removeFromLibrary(element.library);
    } else {
      removeFromEnclosingClass(element.enclosingClass);
    }
  }

  void removeFromEnclosingClass(PartialClassElement cls) {
    cls.localMembersCache = null;
    cls.localMembersReversed = cls.localMembersReversed.copyWithout(element);
    cls.localScope.contents.remove(element.name);
  }

  void removeFromLibrary(LibraryElementX library) {
    library.localMembers = library.localMembers.copyWithout(element);
    library.localScope.contents.remove(element.name);
  }
}

class RemovedFunctionUpdate extends RemovalUpdate
    with FletchFeatures, ReuseFunction {
  final PartialFunctionElement element;

  bool wasStateCaptured = false;

  RemovedFunctionUpdate(Compiler compiler, this.element)
      : super(compiler);

  PartialFunctionElement get before => element;

  PartialFunctionElement get after => null;

  void captureState() {
    if (wasStateCaptured) throw "captureState was called twice.";
    wasStateCaptured = true;
  }

  PartialFunctionElement apply() {
    if (!wasStateCaptured) throw "captureState must be called before apply.";
    removeFromEnclosing();
    reuseElement();
    return null;
  }

  void writeUpdateFletchOn(List<Command> updates) {
    throw new IncrementalCompilationFailed("Not implemented yet.");
  }
}

class RemovedClassUpdate extends RemovalUpdate with FletchFeatures {
  final PartialClassElement element;

  bool wasStateCaptured = false;

  RemovedClassUpdate(Compiler compiler, this.element)
      : super(compiler);

  PartialClassElement get before => element;

  PartialClassElement get after => null;

  void captureState() {
    if (wasStateCaptured) throw "captureState was called twice.";
    wasStateCaptured = true;
  }

  PartialClassElement apply() {
    if (!wasStateCaptured) {
      throw new StateError("captureState must be called before apply.");
    }

    removeFromEnclosing();

    element.forEachLocalMember((ElementX member) {
      compiler.forgetElement(member);
      member.reuseElement();
    });

    compiler.forgetElement(element);
    element.reuseElement();

    return null;
  }

  void writeUpdateFletchOn(List<Command> updates) {
    if (!wasStateCaptured) {
      throw new StateError(
          "captureState must be called before writeUpdateFletchOn.");
    }

    throw new IncrementalCompilationFailed("Not implemented yet.");
  }
}

class RemovedFieldUpdate extends RemovalUpdate with FletchFeatures {
  final FieldElementX element;

  bool wasStateCaptured = false;

  FletchClassBuilder beforeFletchClassBuilder;

  RemovedFieldUpdate(Compiler compiler, this.element)
      : super(compiler);

  PartialFieldList get before => element.declarationSite;

  PartialFieldList get after => null;

  void captureState() {
    if (wasStateCaptured) throw "captureState was called twice.";
    wasStateCaptured = true;
  }

  FieldElementX apply() {
    if (!wasStateCaptured) {
      throw new StateError("captureState must be called before apply.");
    }

    removeFromEnclosing();

    return element;
  }

  void writeUpdateFletchOn(List<Command> updates) {
    if (!wasStateCaptured) {
      throw new StateError(
          "captureState must be called before writeUpdateFletchOn.");
    }
  }
}

class AddedFunctionUpdate extends Update with FletchFeatures {
  final PartialFunctionElement element;

  final /* ScopeContainerElement */ container;

  AddedFunctionUpdate(Compiler compiler, this.element, this.container)
      : super(compiler) {
    if (container == null) {
      throw "container is null";
    }
  }

  PartialFunctionElement get before => null;

  PartialFunctionElement get after => element;

  PartialFunctionElement apply() {
    Element enclosing = container;
    if (enclosing.isLibrary) {
      // TODO(ahe): Reuse compilation unit of element instead?
      enclosing = enclosing.compilationUnit;
    }
    PartialFunctionElement copy = element.copyWithEnclosing(enclosing);
    NO_WARN(container).addMember(copy, compiler);
    return copy;
  }
}

class AddedClassUpdate extends Update with FletchFeatures {
  final PartialClassElement element;

  final LibraryElementX library;

  AddedClassUpdate(Compiler compiler, this.element, this.library)
      : super(compiler);

  PartialClassElement get before => null;

  PartialClassElement get after => element;

  PartialClassElement apply() {
    // TODO(ahe): Reuse compilation unit of element instead?
    CompilationUnitElementX compilationUnit = library.compilationUnit;
    PartialClassElement copy = element.copyWithEnclosing(compilationUnit);
    compilationUnit.addMember(copy, compiler);
    return copy;
  }
}

class AddedFieldUpdate extends Update with FletchFeatures {
  final FieldElementX element;

  final ScopeContainerElement container;

  AddedFieldUpdate(Compiler compiler, this.element, this.container)
      : super(compiler);

  PartialFieldList get before => null;

  PartialFieldList get after => element.declarationSite;

  FieldElementX apply() {
    Element enclosing = container;
    if (enclosing.isLibrary) {
      // TODO(ahe): Reuse compilation unit of element instead?
      enclosing = enclosing.compilationUnit;
    }
    FieldElementX copy = element.copyWithEnclosing(enclosing);
    NO_WARN(container).addMember(copy, compiler);
    return copy;
  }
}


class ClassUpdate extends Update with FletchFeatures {
  final PartialClassElement before;

  final PartialClassElement after;

  ClassUpdate(Compiler compiler, this.before, this.after)
      : super(compiler);

  PartialClassElement apply() {
    patchElement();
    reuseElement();
    return before;
  }

  /// Destructively change the tokens in [before] to match those of [after].
  void patchElement() {
    before.cachedNode = after.cachedNode;
    before.beginToken = after.beginToken;
    before.endToken = after.endToken;
  }

  void reuseElement() {
    before.supertype = null;
    before.interfaces = null;
    before.nativeTagInfo = null;
    before.supertypeLoadState = STATE_NOT_STARTED;
    before.resolutionState = STATE_NOT_STARTED;
    before.isProxy = false;
    before.hasIncompleteHierarchy = false;
    before.backendMembers = const Link<Element>();
    before.allSupertypesAndSelf = null;
  }
}

/// Returns all qualified names in [element] with less than four identifiers. A
/// qualified name is an identifier followed by a sequence of dots and
/// identifiers, for example, "x", and "x.y.z". But not "x.y.z.w" ("w" is the
/// fourth identifier).
///
/// The longest possible name that can be resolved is three identifiers, for
/// example, "prefix.MyClass.staticMethod". Since four or more identifiers
/// cannot resolve to anything statically, they're not included in the returned
/// value of this method.
Set<String> qualifiedNamesIn(PartialElement element) {
  Token beginToken = element.beginToken;
  Token endToken = element.endToken;
  Token token = beginToken;
  if (element is PartialClassElement) {
    ClassNode node = element.cachedNode;
    if (node != null) {
      NodeList body = node.body;
      if (body != null) {
        endToken = body.beginToken;
      }
    }
  }
  Set<String> names = new Set<String>();
  do {
    if (token.isIdentifier()) {
      String name = token.value;
      // [name] is a single "identifier".
      names.add(name);
      if (identical('.', token.next.stringValue) &&
          token.next.next.isIdentifier()) {
        token = token.next.next;
        name += '.${token.value}';
        // [name] is "idenfifier.idenfifier".
        names.add(name);

        if (identical('.', token.next.stringValue) &&
            token.next.next.isIdentifier()) {
          token = token.next.next;
          name += '.${token.value}';
          // [name] is "idenfifier.idenfifier.idenfifier".
          names.add(name);

          while (identical('.', token.next.stringValue) &&
                 token.next.next.isIdentifier()) {
            // Skip remaining identifiers, they cannot statically resolve to
            // anything, and must be dynamic sends.
            token = token.next.next;
          }
        }
      }
    }
    token = token.next;
  } while (token.kind != EOF_TOKEN && token != endToken);
  return names;
}

/// Returns true if one of the qualified names in names (as computed by
/// [qualifiedNamesIn]) could be a static reference to [element].
bool canNamesResolveStaticallyTo(
    Set<String> names,
    Element element,
    /* ScopeContainerElement */ container) {
  if (names.contains(element.name)) return true;
  if (container != null && container.isClass) {
    // [names] contains C.m, where C is the name of [container], and m is the
    // name of [element].
    if (names.contains("${container.name}.${element.name}")) return true;
  }
  // TODO(ahe): Check for prefixes as well.
  return false;
}

DeclarationSite declarationSite(Element element) {
  return element is ElementX ? element.declarationSite : null;
}

abstract class FletchFeatures {
  Compiler get compiler;

  FletchBackend get backend => compiler.backend;

  EnqueueTask get enqueuer => compiler.enqueuer;

  FletchContext get fletchContext => backend.context;

  FletchFunctionBuilder lookupFletchFunctionBuilder(FunctionElement function) {
    return backend.systemBuilder.lookupFunctionBuilderByElement(function);
  }
}

// TODO(ahe): Remove this method.
NO_WARN(x) => x;
