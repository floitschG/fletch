// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

library fletchc.debug_info_constructor_codegen;

import 'package:compiler/src/elements/elements.dart';
import 'package:compiler/src/resolution/resolution.dart';
import 'package:compiler/src/tree/tree.dart';
import 'package:compiler/src/universe/universe.dart';

import 'package:compiler/src/dart2jslib.dart' show
    Registry;

import 'package:compiler/src/dart_types.dart' show
    DartType;

import 'package:compiler/src/util/util.dart' show
    Spannable;

import 'bytecode_assembler.dart';
import 'closure_environment.dart';
import 'codegen_visitor.dart';

import 'fletch_function_builder.dart' show
    FletchFunctionBuilder;

import 'fletch_class_builder.dart' show
    FletchClassBuilder;

import 'fletch_context.dart';
import 'constructor_codegen.dart';
import 'lazy_field_initializer_codegen.dart';
import 'debug_info_lazy_field_initializer_codegen.dart';
import 'debug_info.dart';

class DebugInfoConstructorCodegen extends ConstructorCodegen {
  final DebugInfo debugInfo;
  final FletchCompiler compiler;

  // Regenerate the bytecode in a fresh buffer separately from the compiled
  // function. If we did not create a separate buffer, the bytecode would
  // be appended to the compiled function assembler and we would get a compiled
  // function with incorrect bytecode.
  final BytecodeAssembler debugAssembler;

  DebugInfoConstructorCodegen(this.debugInfo,
                              FletchFunctionBuilder functionBuilder,
                              FletchContext context,
                              TreeElements elements,
                              Registry registry,
                              ClosureEnvironment closureEnvironment,
                              ConstructorElement constructor,
                              FletchClassBuilder classBuilder,
                              this.compiler)
      : debugAssembler = new BytecodeAssembler(functionBuilder.arity),
        super(functionBuilder, context, elements, registry,
              closureEnvironment, constructor, classBuilder);

  BytecodeAssembler get assembler => debugAssembler;

  LazyFieldInitializerCodegen lazyFieldInitializerCodegenFor(
      FletchFunctionBuilder function,
      FieldElement field) {
    TreeElements elements = field.resolvedAst.elements;
    return new DebugInfoLazyFieldInitializerCodegen(
        debugInfo,
        function,
        context,
        elements,
        null,
        context.backend.createClosureEnvironment(field, elements),
        field,
        compiler,
        assembler);
  }

  void recordDebugInfo(Node node) {
    debugInfo.addLocation(compiler, assembler.byteSize, node);
  }

  void pushVariableDeclaration(LocalValue value) {
    super.pushVariableDeclaration(value);
    debugInfo.pushScope(assembler.byteSize, value);
  }

  void popVariableDeclaration(Element element) {
    super.popVariableDeclaration(element);
    debugInfo.popScope(assembler.byteSize);
  }

  void registerDynamicInvocation(Selector selector) { }
  void registerDynamicGetter(Selector selector) { }
  void registerDynamicSetter(Selector selector) { }
  void registerStaticInvocation(FunctionElement function) { }
  void registerInstantiatedClass(ClassElement klass) { }

  void doFieldInitializerSet(Send node, FieldElement field) {
    recordDebugInfo(node);
    super.doFieldInitializerSet(node, field);
  }

  void handleAllocationAndBodyCall() {
    // Clear out debug information after the initializer list. This avoids
    // seeing the code that sets up for the body call as part of the last
    // initializer evaluation.
    recordDebugInfo(null);
    super.handleAllocationAndBodyCall();
  }

  void callIsSelector(
      Node node,
      DartType type,
      Spannable diagnosticLocation) {
    recordDebugInfo(node);
    super.callIsSelector(node, type, diagnosticLocation);
  }

  void invokeMethod(Node node, Selector selector) {
    recordDebugInfo(node);
    super.invokeMethod(node, selector);
  }

  void invokeGetter(Node node, Selector selector) {
    recordDebugInfo(node);
    super.invokeGetter(node, selector);
  }

  void invokeSetter(Node node, Selector selector) {
    recordDebugInfo(node);
    super.invokeSetter(node, selector);
  }

  void invokeFactory(Node node, int constId, int arity) {
    recordDebugInfo(node);
    super.invokeFactory(node, constId, arity);
  }

  void invokeStatic(Node node, int constId, int arity) {
    recordDebugInfo(node);
    super.invokeStatic(node, constId, arity);
  }

  void generateReturn(Node node) {
    recordDebugInfo(node);
    super.generateReturn(node);
  }

  void generateSwitchCaseMatch(CaseMatch caseMatch, BytecodeLabel ifTrue) {
    // We do not want to break on the evaluation of the individual
    // case equality tests.
    recordDebugInfo(null);
    super.generateSwitchCaseMatch(caseMatch, ifTrue);
  }

  void generateIdentical(Node node) {
    recordDebugInfo(node);
    super.generateIdentical(node);
  }

  void generateIdenticalNonNumeric(Node node) {
    recordDebugInfo(node);
    super.generateIdenticalNonNumeric(node);
  }

  void visitForValue(Node node) {
    recordDebugInfo(node);
    super.visitForValue(node);
  }

  void visitForEffect(Node node) {
    recordDebugInfo(node);
    super.visitForEffect(node);
  }
}
