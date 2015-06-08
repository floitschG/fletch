// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

library fletchc.codegen_visitor;

import 'package:compiler/src/resolution/semantic_visitor.dart';

import 'package:compiler/src/resolution/operators.dart' show
    AssignmentOperator,
    BinaryOperator,
    IncDecOperator,
    UnaryOperator;

import 'package:compiler/src/constants/expressions.dart' show
    ConstantExpression,
    ConstructedConstantExpression,
    TypeConstantExpression;

import 'package:compiler/src/dart2jslib.dart' show
    CodegenRegistry,
    MessageKind,
    Registry;

import 'package:compiler/src/util/util.dart' show
    Link;

import 'package:compiler/src/elements/elements.dart';
import 'package:compiler/src/resolution/resolution.dart';
import 'package:compiler/src/tree/tree.dart';
import 'package:compiler/src/universe/universe.dart';
import 'package:compiler/src/util/util.dart' show Spannable;
import 'package:compiler/src/dart_types.dart';

import 'fletch_context.dart';

import 'fletch_backend.dart';

import 'fletch_constants.dart' show
    CompiledFunctionConstant,
    FletchClassConstant,
    FletchClassInstanceConstant;

import '../bytecodes.dart' show
    Bytecode;

import 'compiled_function.dart' show
    CompiledFunction;

import 'fletch_selector.dart';

import 'closure_environment.dart';

enum VisitState {
  Value,
  Effect,
  Test,
}

/**
 * A reference to a local value, including how it should be used
 * (loaded/stored).
 */
abstract class LocalValue {
  final int slot;
  final Element element;
  LocalValue(this.slot, this.element);

  void initialize(BytecodeBuilder builder);

  void load(BytecodeBuilder builder);

  void store(BytecodeBuilder builder);
}

/**
 * A reference to a local value that is boxed.
 */
class BoxedLocalValue extends LocalValue {
  BoxedLocalValue(int slot, Element element) : super(slot, element);

  void initialize(BytecodeBuilder builder) {
    builder.allocateBoxed();
  }

  void load(BytecodeBuilder builder) {
    builder.loadBoxedSlot(slot);
  }

  void store(BytecodeBuilder builder) {
    builder.storeBoxedSlot(slot);
  }

  String toString() => "Boxed($element, $slot)";
}

/**
 * A reference to a local value that is boxed.
 */
class UnboxedLocalValue extends LocalValue {
  UnboxedLocalValue(int slot, Element element) : super(slot, element);

  void initialize(BytecodeBuilder builder) {}

  void load(BytecodeBuilder builder) {
    builder.loadSlot(slot);
  }

  void store(BytecodeBuilder builder) {
    builder.storeSlot(slot);
  }

  String toString() => "Local($element, $slot)";
}

class JumpInfo {
  final int stackSize;
  final BytecodeLabel continueLabel;
  final BytecodeLabel breakLabel;
  JumpInfo(this.stackSize, this.continueLabel, this.breakLabel);
}

class TryBlock {
  final int stackSize;
  final BytecodeLabel finallyLabel;
  final BytecodeLabel finallyReturnLabel;
  TryBlock(this.stackSize, this.finallyLabel, this.finallyReturnLabel);
}

abstract class CodegenVisitor
    extends SemanticVisitor
    with GetBulkMixin,
         SetBulkMixin,
         ErrorBulkMixin,
         InvokeBulkMixin,
         IndexSetBulkMixin,
         CompoundBulkMixin,
         UnaryBulkMixin,
         BaseBulkMixin,
         BinaryBulkMixin,
         PrefixBulkMixin,
         PostfixBulkMixin,
         NewBulkMixin,
         VariableBulkMixin,
         ParameterBulkMixin,
         FunctionBulkMixin,
         ConstructorBulkMixin,
         InitializerBulkMixin,
         BaseImplementationOfStaticsMixin,
         BaseImplementationOfLocalsMixin
    implements SemanticSendVisitor, SemanticDeclarationVisitor {
  // A literal int can have up to 31 bits of information (32 minus sign).
  static const int LITERAL_INT_MAX = 0x3FFFFFFF;

  final FletchContext context;

  final CodegenRegistry registry;

  final ClosureEnvironment closureEnvironment;

  final ExecutableElement element;

  final MemberElement member;

  final CompiledFunction compiledFunction;

  final Map<Element, LocalValue> scope = <Element, LocalValue>{};

  final Map<Node, JumpInfo> jumpInfo = <Node, JumpInfo>{};

  // Stack of try blocks (inner-most first), in the lexical scope.
  Link<TryBlock> tryBlockStack = const Link<TryBlock>();

  VisitState visitState;
  BytecodeLabel trueLabel;
  BytecodeLabel falseLabel;

  // TODO(ajohnsen): Merge computation into constructor.
  // The slot at which 'this' is stored. In closures, this is overwritten.
  LocalValue thisValue;

  List<Element> blockLocals = <Element>[];

  CodegenVisitor(CompiledFunction compiledFunction,
                 this.context,
                 TreeElements elements,
                 this.registry,
                 this.closureEnvironment,
                 this.element)
      : super(elements),
        this.compiledFunction = compiledFunction,
        thisValue = new UnboxedLocalValue(
            -1 - compiledFunction.builder.functionArity,
            null);

  BytecodeBuilder get builder => compiledFunction.builder;

  SemanticSendVisitor get sendVisitor => this;
  SemanticDeclarationVisitor get declVisitor => this;

  void compile();

  ConstantExpression compileConstant(
      Node node,
      {TreeElements elements,
       bool isConst}) {
    if (elements == null) elements = this.elements;
    return context.compileConstant(node, elements, isConst: isConst);
  }

  int allocateConstantFromNode(Node node, {TreeElements elements}) {
    ConstantExpression expression = compileConstant(
        node,
        elements: elements,
        isConst: false);
    return compiledFunction.allocateConstant(expression.value);
  }

  int allocateConstantClassInstance(int classId) {
    var constant = new FletchClassInstanceConstant(classId);
    context.markConstantUsed(constant);
    return compiledFunction.allocateConstant(constant);
  }

  int allocateStringConstant(String string) {
    return compiledFunction.allocateConstant(
        context.backend.constantSystem.createString(
            new DartString.literal(string)));
  }

  ClosureInfo get closureInfo => closureEnvironment.closures[element];

  LocalValue createLocalValueFor(
      LocalElement element,
      [int slot]) {
    if (slot == null) slot = builder.stackSize;
    if (closureEnvironment.shouldBeBoxed(element)) {
      return new BoxedLocalValue(slot, element);
    }
    return new UnboxedLocalValue(slot, element);
  }

  LocalValue createLocalValueForParameter(
      ParameterElement parameter,
      int slot) {
    if (closureEnvironment.shouldBeBoxed(parameter)) {
      LocalValue value = new BoxedLocalValue(builder.stackSize, parameter);
      builder.loadSlot(slot);
      value.initialize(builder);
      return value;
    }
    return new UnboxedLocalValue(slot, parameter);
  }

  void pushVariableDeclaration(LocalValue value) {
    scope[value.element] = value;
  }

  void popVariableDeclaration(Element local) {
    scope.remove(local);
  }

  void registerDynamicInvocation(Selector selector) {
    registry.registerDynamicInvocation(selector);
  }

  void registerDynamicGetter(Selector selector) {
    registry.registerDynamicGetter(selector);
  }

  void registerDynamicSetter(Selector selector) {
    registry.registerDynamicSetter(selector);
  }

  void registerStaticInvocation(FunctionElement function) {
    registry.registerStaticInvocation(function);
  }

  void registerInstantiatedClass(ClassElement klass) {
    registry.registerInstantiatedClass(klass);
  }

  void invokeMethod(Node node, Selector selector) {
    registerDynamicInvocation(selector);
    String symbol = context.getSymbolFromSelector(selector);
    int id = context.getSymbolId(symbol);
    int arity = selector.argumentCount;
    int fletchSelector = FletchSelector.encodeMethod(id, arity);
    builder.invokeMethod(fletchSelector, arity, selector.name);
  }

  void invokeGetter(Node node, Selector selector) {
    registerDynamicGetter(selector);
    String symbol = context.getSymbolFromSelector(selector);
    int id = context.getSymbolId(symbol);
    int fletchSelector = FletchSelector.encodeGetter(id);
    builder.invokeMethod(fletchSelector, 0);
  }

  void invokeSetter(Node node, Selector selector) {
    registerDynamicSetter(selector);
    String symbol = context.getSymbolFromSelector(selector);
    int id = context.getSymbolId(symbol);
    int fletchSelector = FletchSelector.encodeSetter(id);
    builder.invokeMethod(fletchSelector, 1);
  }

  void invokeFactory(Node node, int constId, int arity) {
    builder.invokeFactory(constId, arity);
  }

  void invokeStatic(Node node, int constId, int arity) {
    builder.invokeStatic(constId, arity);
  }

  void generateIdentical(Node node) {
    builder.identical();
  }

  void generateIdenticalNonNumeric(Node node) {
    builder.identicalNonNumeric();
  }

  void generateReturn(Node node) {
    builder.ret();
  }

  void generateSwitchCaseMatch(CaseMatch caseMatch, BytecodeLabel ifTrue) {
    builder.dup();
    int constId = allocateConstantFromNode(caseMatch.expression);
    builder.loadConst(constId);
    // For debugging, ignore the equality checks in connection
    // with case matches by not associating the calls with
    // any node.
    invokeMethod(null, new Selector.binaryOperator('=='));
    builder.branchIfTrue(ifTrue);
  }

  CompiledFunction requireCompiledFunction(FunctionElement element) {
    registerStaticInvocation(element);
    return context.backend.createCompiledFunction(element);
  }

  void doStaticFunctionInvoke(
      Node node,
      CompiledFunction function,
      NodeList arguments,
      CallStructure callStructure,
      {bool factoryInvoke: false}) {
    if (function.hasThisArgument) loadThis();
    FunctionSignature signature = function.signature;
    int methodId;
    int arity;
    if (signature.hasOptionalParameters &&
        signature.optionalParametersAreNamed) {
      // TODO(ajohnsen): Don't use selectors.
      if (function.matchesSelector(callStructure.callSelector)) {
        methodId = function.methodId;
      } else if (function.canBeCalledAs(callStructure.callSelector)) {
        // TODO(ajohnsen): Inline parameter mapping?
        CompiledFunction stub = function.createParameterMappingFor(
            callStructure.callSelector, context);
        methodId = stub.methodId;
      } else {
        doUnresolved(function.name);
        return;
      }
      for (Node argument in arguments) {
        visitForValue(argument);
      }
      arity = callStructure.argumentCount;
    } else if (callStructure != null &&
               callStructure.namedArguments.isNotEmpty) {
      doUnresolved(function.name);
      return;
    } else {
      methodId = function.methodId;
      arity = loadPositionalArguments(arguments, signature, function.name);
    }
    if (function.hasThisArgument) arity++;
    int constId = compiledFunction.allocateConstantFromFunction(methodId);
    if (factoryInvoke) {
      invokeFactory(node, constId, arity);
    } else {
      invokeStatic(node, constId, arity);
    }
  }

  void loadThis() {
    thisValue.load(builder);
  }

  /**
   * Load the [arguments] for caling [function], with potential positional
   * arguments.
   *
   * Return the number of arguments pushed onto the stack.
   */
  int loadPositionalArguments(
      NodeList arguments,
      FunctionSignature signature,
      String name) {
    int argumentCount = 0;
    Iterator<Node> it = arguments.iterator;
    signature.orderedForEachParameter((ParameterElement parameter) {
      if (it.moveNext()) {
        visitForValue(it.current);
      } else {
        if (parameter.isOptional) {
          doParameterInitializer(parameter);
        } else {
          doUnresolved(name);
        }
      }
      argumentCount++;
    });
    if (it.moveNext()) doUnresolved(name);
    return argumentCount;
  }

  void doParameterInitializer(ParameterElement parameter) {
    Expression initializer = parameter.initializer;
    if (initializer == null) {
      builder.loadLiteralNull();
    } else {
      int constId = allocateConstantFromNode(
          initializer,
          elements: parameter.resolvedAst.elements);
      builder.loadConst(constId);
    }
  }

  // Visit the expression [node] with the result pushed on top of the stack.
  void visitForValue(Node node) {
    VisitState oldState = visitState;
    visitState = VisitState.Value;
    node.accept(this);
    visitState = oldState;
  }

  // Visit the expression [node] without the result pushed on top of the stack.
  void visitForEffect(Node node) {
    VisitState oldState = visitState;
    visitState = VisitState.Effect;
    node.accept(this);
    visitState = oldState;
  }

  // Visit the expression [node] with the result being a branch to either
  // [trueLabel] or [falseLabel].
  void visitForTest(
      Node node,
      BytecodeLabel trueLabel,
      BytecodeLabel falseLabel) {
    VisitState oldState = visitState;
    visitState = VisitState.Test;
    BytecodeLabel oldTrueLabel = this.trueLabel;
    this.trueLabel = trueLabel;
    BytecodeLabel oldFalseLabel = this.falseLabel;
    this.falseLabel = falseLabel;
    node.accept(this);
    visitState = oldState;
    this.trueLabel = oldTrueLabel;
    this.falseLabel = oldFalseLabel;
  }

  void applyVisitState() {
    if (visitState == VisitState.Effect) {
      builder.pop();
    } else if (visitState == VisitState.Test) {
      builder.branchIfTrue(trueLabel);
      builder.branch(falseLabel);
    }
  }

  void visitNamedArgument(NamedArgument node) {
    Expression expression = node.expression;
    if (expression != null) {
      visitForValue(expression);
    } else {
      builder.loadLiteralNull();
    }
    applyVisitState();
  }

  void doLocalVariableCompound(
      Node node,
      LocalVariableElement variable,
      AssignmentOperator operator,
      Node rhs) {
    LocalValue value = scope[variable];
    value.load(builder);
    visitForValue(rhs);
    String operatorName = operator.binaryOperator.name;
    invokeMethod(node, new Selector.binaryOperator(operatorName));
    value.store(builder);
  }

  void visitLocalVariableCompound(
      Send node,
      LocalVariableElement variable,
      AssignmentOperator operator,
      Node rhs,
      _) {
    doLocalVariableCompound(node, variable, operator, rhs);
    applyVisitState();
  }

  void visitParameterCompound(
      Send node,
      LocalParameterElement parameter,
      AssignmentOperator operator,
      Node rhs,
      _) {
    doLocalVariableCompound(node, parameter, operator, rhs);
    applyVisitState();
  }

  void doStaticFieldCompound(
      Node node,
      FieldElement field,
      AssignmentOperator operator,
      Node rhs) {
    doStaticFieldGet(field);
    visitForValue(rhs);
    Selector selector = new Selector.binaryOperator(
        operator.binaryOperator.name);
    invokeMethod(node, selector);
    doStaticFieldSet(field);
  }

  void visitTopLevelFieldCompound(
      Send node,
      FieldElement field,
      AssignmentOperator operator,
      Node rhs,
      _) {
    doStaticFieldCompound(node, field, operator, rhs);
    applyVisitState();
  }

  void visitStaticFieldCompound(
      Send node,
      FieldElement field,
      AssignmentOperator operator,
      Node rhs,
      _) {
    doStaticFieldCompound(node, field, operator, rhs);
    applyVisitState();
  }

  void doBinaryOperator(
      Node node,
      Node left,
      Node right,
      BinaryOperator operator) {
    bool isConstNull(Node node) {
      ConstantExpression expression = compileConstant(node, isConst: false);
      if (expression == null) return false;
      return expression.value.isNull;
    }

    visitForValue(left);
    visitForValue(right);
    // For '==', if either side is a null literal, use identicalNonNumeric.
    if (operator == BinaryOperator.EQ &&
        (isConstNull(left) || isConstNull(right))) {
      generateIdenticalNonNumeric(node);
      return;
    }

    Selector selector = new Selector.binaryOperator(operator.name);
    invokeMethod(node, selector);
  }

  void visitEquals(
      Send node,
      Node left,
      Node right,
      _) {
    // TODO(ajohnsen): Inject null check (in callee).
    doBinaryOperator(node, left, right, BinaryOperator.EQ);
    applyVisitState();
  }

  void visitNotEquals(
      Send node,
      Node left,
      Node right,
      _) {
    doBinaryOperator(node, left, right, BinaryOperator.EQ);
    if (visitState == VisitState.Test) {
      builder.branchIfTrue(falseLabel);
      builder.branch(trueLabel);
    } else {
      builder.negate();
      applyVisitState();
    }
  }

  void visitBinary(
      Send node,
      Node left,
      BinaryOperator operator,
      Node right,
      _) {
    doBinaryOperator(node, left, right, operator);
    applyVisitState();
  }

  void visitUnary(
      Send node,
      UnaryOperator operator,
      Node value,
      _) {
    visitForValue(value);
    Selector selector = new Selector.unaryOperator(operator.name);
    invokeMethod(node, selector);
    applyVisitState();
  }

  void visitNot(
      Send node,
      Node value,
      _) {
    visitForValue(value);
    builder.negate();
    applyVisitState();
  }

  void visitIndex(
      Send node,
      Node receiver,
      Node index,
      _) {
    visitForValue(receiver);
    visitForValue(index);
    Selector selector = new Selector.index();
    invokeMethod(node, selector);
    applyVisitState();
  }

  void visitIndexSet(
      SendSet node,
      Node receiver,
      Node index,
      Node value,
      _) {
    visitForValue(receiver);
    visitForValue(index);
    visitForValue(value);
    Selector selector = new Selector.indexSet();
    invokeMethod(node, selector);
    applyVisitState();
  }

  void visitLogicalAnd(
      Send node,
      Node left,
      Node right,
      _) {
    if (visitState == VisitState.Test) {
      BytecodeLabel isFirstTrue = new BytecodeLabel();
      visitForTest(left, isFirstTrue, falseLabel);
      builder.bind(isFirstTrue);
      visitForTest(right, trueLabel, falseLabel);
      return;
    }

    BytecodeLabel isFirstTrue = new BytecodeLabel();
    BytecodeLabel isTrue = new BytecodeLabel();
    BytecodeLabel isFalse = new BytecodeLabel();
    BytecodeLabel done = new BytecodeLabel();

    builder.loadLiteralFalse();

    visitForTest(left, isFirstTrue, isFalse);

    builder.bind(isFirstTrue);
    visitForTest(right, isTrue, isFalse);

    builder.bind(isTrue);
    builder.pop();
    builder.loadLiteralTrue();
    builder.bind(isFalse);

    applyVisitState();
  }

  void visitLogicalOr(
      Send node,
      Node left,
      Node right,
      _) {
    if (visitState == VisitState.Test) {
      BytecodeLabel isFirstFalse = new BytecodeLabel();
      visitForTest(left, trueLabel, isFirstFalse);
      builder.bind(isFirstFalse);
      visitForTest(right, trueLabel, falseLabel);
      return;
    }

    BytecodeLabel isFirstFalse = new BytecodeLabel();
    BytecodeLabel isTrue = new BytecodeLabel();
    BytecodeLabel isFalse = new BytecodeLabel();
    BytecodeLabel done = new BytecodeLabel();

    builder.loadLiteralTrue();

    visitForTest(left, isTrue, isFirstFalse);

    builder.bind(isFirstFalse);
    visitForTest(right, isTrue, isFalse);

    builder.bind(isFalse);
    builder.pop();
    builder.loadLiteralFalse();
    builder.bind(isTrue);

    applyVisitState();
  }

  void visitConditional(Conditional node) {
    BytecodeLabel isTrue = new BytecodeLabel();
    BytecodeLabel isFalse = new BytecodeLabel();
    BytecodeLabel done = new BytecodeLabel();

    builder.loadLiteralNull();

    visitForTest(node.condition, isTrue, isFalse);

    builder.bind(isTrue);
    builder.pop();
    visitForValue(node.thenExpression);
    builder.branch(done);

    builder.bind(isFalse);
    builder.pop();
    visitForValue(node.elseExpression);

    builder.bind(done);

    applyVisitState();
  }

  void callIsSelector(
      Node node,
      DartType type,
      // TODO(ahe): Remove [diagnosticLocation] when malformed types are
      // handled.
      Spannable diagnosticLocation) {
    if (type == null || type.isMalformed) {
      builder.pop();
      generateUnimplementedError(
          diagnosticLocation, "Unhandled type test for malformed $type.");
      return;
    }

    if (type.isDynamic) {
      builder.pop();
      builder.loadLiteralTrue();
      return;
    }

    if (!type.isInterfaceType) {
      builder.pop();
      generateUnimplementedError(
          diagnosticLocation, "Unhandled type test for $type.");
      return;
    }

    Element element = type.element;
    int fletchSelector = context.toFletchIsSelector(element);
    builder.invokeTest(fletchSelector, 0);
  }

  void doIs(
      Node node,
      Node expression,
      DartType type,
      // TODO(ahe): Remove [diagnosticLocation] when callIsSelector does not
      // require it.
      Spannable diagnosticLocation) {
    visitForValue(expression);
    callIsSelector(node, type, diagnosticLocation);
  }

  void visitIs(
      Send node,
      Node expression,
      DartType type,
      _) {
    doIs(node, expression, type, node.arguments.first);
    applyVisitState();
  }

  void visitIsNot(
      Send node,
      Node expression,
      DartType type,
      _) {
    doIs(node, expression, type, node.arguments.first);
    builder.negate();
    applyVisitState();
  }

  void visitAs(
      Send node,
      Node expression,
      DartType type,
      _) {
    // TODO(ajohnsen): To actual type check.
    visitForValue(expression);
    applyVisitState();
  }

  void visitThisGet(
      Node node,
      _) {
    loadThis();
    applyVisitState();
  }

  void doIdenticalCall(Node node, NodeList arguments) {
    assert(arguments.slowLength() == 2);
    for (Node argument in arguments) {
      visitForValue(argument);
    }
    generateIdentical(node);
  }

  void handleStaticFunctionGet(
      Send node,
      MethodElement function,
      _) {
    CompiledFunction compiledFunctionTarget = requireCompiledFunction(function);
    CompiledClass compiledClass = context.backend.createTearoffClass(
        compiledFunctionTarget);
    assert(compiledClass.fields == 0);
    int constId = allocateConstantClassInstance(compiledClass.id);
    builder.loadConst(constId);
    applyVisitState();
  }

  void doMainCall(Send node, NodeList arguments) {
    FunctionElement function = context.compiler.mainFunction;
    if (function.isErroneous) {
      doCompileError();
      return;
    }
    if (context.compiler.libraryLoader.libraries.any(checkCompileError)) return;
    registerStaticInvocation(function);
    // Load up to 'parameterCount' arguments, padding with nulls.
    int parameterCount = function.functionSignature.parameterCount;
    int argumentCount = 0;
    for (Node argument in arguments) {
      if (argumentCount == parameterCount) break;
      visitForValue(argument);
      argumentCount++;
    }
    for (int i = argumentCount; i < parameterCount; i++) {
      builder.loadLiteralNull();
    }
    int methodId = context.backend.functionMethodId(function);
    int constId = compiledFunction.allocateConstantFromFunction(methodId);
    invokeStatic(node, constId, parameterCount);
  }

  void doStaticallyBoundInvoke(
      Send node,
      MethodElement element,
      NodeList arguments,
      CallStructure callStructure) {
    if (checkCompileError(element)) return;
    if (element.declaration == context.compiler.identicalFunction) {
      doIdenticalCall(node, arguments);
      return;
    }
    if (element.isExternal) {
      // Patch known functions directly.
      if (element == context.backend.fletchExternalInvokeMain) {
        doMainCall(node, arguments);
        return;
      } else if (element == context.backend.fletchExternalCoroutineChange) {
        for (Node argument in arguments) {
          visitForValue(argument);
        }
        builder.coroutineChange();
        return;
      }
      // TODO(ajohnsen): Define a known set of external functions we allow
      // calls to?
    }
    CompiledFunction target = requireCompiledFunction(element);
    doStaticFunctionInvoke(node, target, arguments, callStructure);
  }

  void handleStaticFunctionInvoke(
      Send node,
      MethodElement element,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    doStaticallyBoundInvoke(
        node, element.declaration, arguments, callStructure);
    applyVisitState();
  }

  void visitSuperMethodInvoke(
      Send node,
      MethodElement element,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    doStaticallyBoundInvoke(node, element, arguments, callStructure);
    applyVisitState();
  }

  void doSuperCall(Node node, FunctionElement function) {
    registerStaticInvocation(function);
    int arity = function.functionSignature.parameterCount + 1;
    int methodId = context.backend.functionMethodId(function);
    int constId = compiledFunction.allocateConstantFromFunction(methodId);
    invokeStatic(node, constId, arity);
  }

  void visitSuperGetterGet(
      Send node,
      FunctionElement getter,
      _) {
    loadThis();
    doSuperCall(node, getter);
    applyVisitState();
  }

  void visitSuperMethodGet(
      Send node,
      MethodElement method,
      _) {
    loadThis();
    CompiledFunction compiledFunctionTarget = requireCompiledFunction(method);
    CompiledClass compiledClass = context.backend.createTearoffClass(
        compiledFunctionTarget);
    assert(compiledClass.fields == 1);
    int constId = compiledFunction.allocateConstantFromClass(compiledClass.id);
    builder.allocate(constId, compiledClass.fields);
    applyVisitState();
  }

  void visitSuperSetterSet(
      SendSet node,
      FunctionElement setter,
      Node rhs,
      _) {
    loadThis();
    visitForValue(rhs);
    doSuperCall(node, setter);
    applyVisitState();
  }

  void visitSuperIndex(
      Send node,
      FunctionElement function,
      Node index,
      _) {
    loadThis();
    visitForValue(index);
    doSuperCall(node, function);
    applyVisitState();
  }

  void visitSuperIndexSet(
      Send node,
      FunctionElement function,
      Node index,
      Node rhs,
      _) {
    loadThis();
    visitForValue(index);
    visitForValue(rhs);
    doSuperCall(node, function);
    applyVisitState();
  }

  void visitSuperCompoundIndexSet(
      Send node,
      FunctionElement getter,
      FunctionElement setter,
      Node index,
      AssignmentOperator operator,
      Node rhs,
      _) {
    visitForValue(index);
    loadThis();
    builder.loadLocal(1);
    doSuperCall(node, getter);
    loadThis();
    // Load index
    builder.loadLocal(2);
    // Load value from index call and call operator.
    builder.loadLocal(2);
    visitForValue(rhs);
    invokeMethod(node, getAssignmentSelector(operator));
    doSuperCall(node, setter);
    // Override 'index' with result value, and pop everything else.
    builder.storeLocal(2);
    builder.pop();
    builder.pop();
    applyVisitState();
  }

  void visitSuperIndexPostfix(
      SendSet node,
      FunctionElement getter,
      FunctionElement setter,
      Node index,
      IncDecOperator operator,
      _) {
    // TODO(ajohnsen): Fast-case when for effect.
    visitForValue(index);
    loadThis();
    builder.loadLocal(1);
    doSuperCall(node, getter);
    loadThis();
    // Load index
    builder.loadLocal(2);
    // Load value from index call and inc/dec.
    builder.loadLocal(2);
    builder.loadLiteral(1);
    invokeMethod(node, getIncDecSelector(operator));
    // We can now call []= with 'this', 'index' and 'value'.
    doSuperCall(node, setter);
    builder.pop();
    // Pop result, override 'index' with initial indexed value, and pop again.
    builder.storeLocal(1);
    builder.pop();
    applyVisitState();
  }

  void visitSuperBinary(
      Send node,
      FunctionElement function,
      BinaryOperator operator,
      Node argument,
      _) {
    loadThis();
    visitForValue(argument);
    doSuperCall(node, function);
    applyVisitState();
  }

  void visitSuperEquals(
      Send node,
      FunctionElement function,
      Node argument,
      _) {
    loadThis();
    visitForValue(argument);
    doSuperCall(node, function);
    applyVisitState();
  }

  void visitSuperUnary(
      Send node,
      UnaryOperator operator,
      FunctionElement function,
      _) {
    loadThis();
    doSuperCall(node, function);
    applyVisitState();
  }

  int computeFieldIndex(FieldElement field) {
    ClassElement classElement = field.enclosingClass;
    // We know the enclosing class is compiled, so we can use the CompiledClass
    // as an optimization for getting the number of super fields, thus we only
    // have to iterate the fields of the enclosing class.
    CompiledClass compiledClass = context.backend.registerClassElement(
        classElement);
    int i = 0;
    int fieldIndex;
    classElement.implementation.forEachInstanceField((_, FieldElement member) {
      if (member == field) {
        assert(fieldIndex == null);
        fieldIndex = i;
      }
      i++;
    });
    assert(fieldIndex != null);
    fieldIndex += compiledClass.superclassFields;
    return fieldIndex;
  }

  void visitSuperFieldGet(
      Send node,
      FieldElement field,
      _) {
    loadThis();
    builder.loadField(computeFieldIndex(field));
    applyVisitState();
  }

  void visitSuperFieldSet(
      SendSet node,
      FieldElement field,
      Node rhs,
      _) {
    loadThis();
    visitForValue(rhs);
    builder.storeField(computeFieldIndex(field));
    applyVisitState();
  }

  void handleStaticFieldInvoke(
      Node node,
      FieldElement field,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    doStaticFieldGet(field);
    for (Node argument in arguments) {
      visitForValue(argument);
    }
    invokeMethod(node, callStructure.callSelector);
    applyVisitState();
  }

  void visitDynamicPropertyInvoke(
      Send node,
      Node receiver,
      NodeList arguments,
      Selector selector,
      _) {
    if (selector == null) {
      // TODO(ajohnsen): Remove hack - dart2js has a problem with generating
      // selectors in initializer bodies.
      selector = new Selector.call(
          node.selector.asIdentifier().source,
          element.library,
          arguments.slowLength());
    }
    visitForValue(receiver);
    for (Node argument in arguments) {
      visitForValue(argument);
    }
    invokeMethod(node, selector);
    applyVisitState();
  }

  void visitExpressionInvoke(
      Send node,
      Expression receiver,
      NodeList arguments,
      Selector selector,
      _) {
    visitForValue(receiver);
    for (Node argument in arguments) {
      visitForValue(argument);
    }
    invokeMethod(node, selector);
    applyVisitState();
  }

  void visitThisPropertyInvoke(
      Send node,
      NodeList arguments,
      Selector selector,
      _) {
    loadThis();

    // If the property is statically known to be a field, instead invoke the
    // getter and then invoke 'call(...)' on the value.
    // TODO(ajohnsen): This is a fix that only works when the field is
    // statically known - that is not always the case. Implement VM support?
    Element target = elements[node];
    if (target != null && target.isField) {
      invokeGetter(node, new Selector.getter(target.name, element.library));
      selector = new Selector.callClosureFrom(selector);
    }
    for (Node argument in arguments) {
      visitForValue(argument);
    }
    invokeMethod(node, selector);
    applyVisitState();
  }

  void visitThisInvoke(
      Send node,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    loadThis();
    for (Node argument in arguments) {
      visitForValue(argument);
    }
    invokeMethod(node, callStructure.callSelector);
    applyVisitState();
  }

  void visitDynamicPropertyGet(
      Send node,
      Node receiver,
      Selector selector,
      _) {
    if (selector == null) {
      // TODO(ajohnsen): Remove hack - dart2js has a problem with generating
      // selectors in initializer bodies.
      selector = new Selector.getter(
          node.selector.asIdentifier().source,
          element.library);
    }
    visitForValue(receiver);
    invokeGetter(node, selector);
    applyVisitState();
  }

  void visitThisPropertyGet(
      Send node,
      Selector selector,
      _) {
    loadThis();
    invokeGetter(node, selector);
    applyVisitState();
  }

  void visitThisPropertySet(
      Send node,
      Selector selector,
      Node rhs,
      _) {
    loadThis();
    visitForValue(rhs);
    invokeSetter(node, selector);
    applyVisitState();
  }

  void doStaticFieldGet(FieldElement field) {
    if (checkCompileError(field)) return;
    if (field.isConst) {
      int constId = allocateConstantFromNode(
          field.initializer,
          elements: field.resolvedAst.elements);
      builder.loadConst(constId);
    } else {
      int index = context.backend.compileLazyFieldInitializer(field, registry);
      if (field.initializer != null) {
        builder.loadStaticInit(index);
      } else {
        builder.loadStatic(index);
      }
    }
  }

  void handleStaticFieldGet(
      Send node,
      FieldElement field,
      _) {
    doStaticFieldGet(field);
    applyVisitState();
  }

  void visitAssert(Send node, Node expression, _) {
    // TODO(ajohnsen): Emit assert in checked mode.
  }

  void visitDynamicPropertySet(
      Send node,
      Node receiver,
      Selector selector,
      Node rhs,
      _) {
    visitForValue(receiver);
    visitForValue(rhs);
    invokeSetter(node, selector);
    applyVisitState();
  }

  void doStaticFieldSet(FieldElement field) {
    int index = context.getStaticFieldIndex(field, element);
    builder.storeStatic(index);
  }

  void handleStaticFieldSet(
      SendSet node,
      FieldElement field,
      Node rhs,
      _) {
    visitForValue(rhs);
    doStaticFieldSet(field);
    applyVisitState();
  }

  void visitStringJuxtaposition(StringJuxtaposition node) {
    // TODO(ajohnsen): This could probably be optimized to string constants in
    // some cases.
    visitForValue(node.first);
    visitForValue(node.second);
    // TODO(ajohnsen): Cache these in context/backend.
    Selector concat = new Selector.binaryOperator('+');
    invokeMethod(node, concat);
    applyVisitState();
  }

  void visitStringInterpolation(StringInterpolation node) {
    // TODO(ajohnsen): Cache these in context/backend.
    Selector toString = new Selector.call('toString', null, 0);
    Selector concat = new Selector.binaryOperator('+');
    visitForValue(node.string);
    for (StringInterpolationPart part in node.parts) {
      visitForValue(part.expression);
      invokeMethod(node, toString);
      visitForValue(part.string);
      invokeMethod(node, concat);
      invokeMethod(node, concat);
    }
    applyVisitState();
  }

  void visitLiteralNull(LiteralNull node) {
    if (visitState == VisitState.Value) {
      builder.loadLiteralNull();
    } else if (visitState == VisitState.Test) {
      builder.branch(falseLabel);
    }
  }

  void visitLiteralSymbol(LiteralSymbol node) {
    int constId = allocateConstantFromNode(node);
    builder.loadConst(constId);
  }

  void visitLiteralBool(LiteralBool node) {
    var expression = compileConstant(node, isConst: false);
    bool isTrue = expression != null && expression.value.isTrue;

    if (visitState == VisitState.Value) {
      if (isTrue) {
        builder.loadLiteralTrue();
      } else {
        builder.loadLiteralFalse();
      }
    } else if (visitState == VisitState.Test) {
      builder.branch(isTrue ? trueLabel : falseLabel);
    }
  }

  void visitLiteralInt(LiteralInt node) {
    if (visitState == VisitState.Value) {
      int value = node.value;
      assert(value >= 0);
      if (value > LITERAL_INT_MAX) {
        int constId = allocateConstantFromNode(node);
        builder.loadConst(constId);
      } else {
        builder.loadLiteral(value);
      }
    } else if (visitState == VisitState.Test) {
      builder.branch(falseLabel);
    }
  }

  void visitLiteral(Literal node) {
    if (visitState == VisitState.Value) {
      builder.loadConst(allocateConstantFromNode(node));
    } else if (visitState == VisitState.Test) {
      builder.branch(falseLabel);
    }
  }

  void visitLiteralList(LiteralList node) {
    if (node.isConst) {
      int constId = allocateConstantFromNode(node);
      builder.loadConst(constId);
      applyVisitState();
      return;
    }
    ClassElement literalClass = context.backend.growableListClass;
    ConstructorElement constructor = literalClass.lookupDefaultConstructor();
    if (constructor == null) {
      internalError(node, "Failed to lookup default list constructor");
    }
    // Call with empty arguments, as we call the default constructor.
    callConstructor(
        node, constructor, new NodeList.empty(), CallStructure.NO_ARGS);
    Selector add = new Selector.call('add', null, 1);
    for (Node element in node.elements) {
      builder.dup();
      visitForValue(element);
      invokeMethod(node, add);
      builder.pop();
    }
    applyVisitState();
  }

  void visitLiteralMap(LiteralMap node) {
    if (node.isConst) {
      int constId = allocateConstantFromNode(node);
      builder.loadConst(constId);
      applyVisitState();
      return;
    }
    ClassElement literalClass = context.backend.linkedHashMapClass;
    ConstructorElement constructor = literalClass.lookupDefaultConstructor();
    if (constructor == null) {
      internalError(literalClass,
                    "Failed to lookup default list constructor");
      return;
    }
    callConstructor(
        node, constructor, new NodeList.empty(), CallStructure.NO_ARGS);
    Selector selector = new Selector.indexSet();
    for (Node element in node.entries) {
      builder.dup();
      visitForValue(element);
      invokeMethod(node, selector);
      builder.pop();
    }
    applyVisitState();
  }

  void visitLiteralMapEntry(LiteralMapEntry node) {
    assert(visitState == VisitState.Value);
    visitForValue(node.key);
    visitForValue(node.value);
  }

  void visitLiteralString(LiteralString node) {
    if (visitState == VisitState.Value) {
      builder.loadConst(allocateConstantFromNode(node));
      registerInstantiatedClass(
          context.compiler.backend.stringImplementation);
    } else if (visitState == VisitState.Test) {
      builder.branch(falseLabel);
    }
  }

  void visitCascadeReceiver(CascadeReceiver node) {
    visitForValue(node.expression);
    builder.dup();
    assert(visitState == VisitState.Value);
  }

  void visitCascade(Cascade node) {
    visitForEffect(node.expression);
    applyVisitState();
  }

  void visitParenthesizedExpression(ParenthesizedExpression node) {
    // Visit expression in the same VisitState.
    node.expression.accept(this);
  }

  void handleLocalGet(
      Send node,
      LocalElement element,
      _) {
    scope[element].load(builder);
    applyVisitState();
  }

  void handleLocalSet(
      SendSet node,
      LocalElement element,
      Node rhs,
      _) {
    visitForValue(rhs);
    scope[element].store(builder);
    applyVisitState();
  }

  void handleLocalInvoke(
      Node node,
      LocalElement element,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    scope[element].load(builder);
    for (Node argument in arguments) {
      visitForValue(argument);
    }
    invokeMethod(node, callStructure.callSelector);
    applyVisitState();
  }

  static Selector getIncDecSelector(IncDecOperator operator) {
    String name = operator == IncDecOperator.INC ? '+' : '-';
    return new Selector.binaryOperator(name);
  }

  static Selector getAssignmentSelector(AssignmentOperator operator) {
    String name = operator.binaryOperator.name;
    return new Selector.binaryOperator(name);
  }

  void doLocalVariableIncrement(
      Node node,
      LocalVariableElement element,
      IncDecOperator operator,
      bool prefix) {
    // TODO(ajohnsen): Candidate for bytecode: Inc/Dec local with non-Smi
    // bailout.
    LocalValue value = scope[element];
    value.load(builder);
    // For postfix, keep local, unmodified version, to 'return' after store.
    if (!prefix) builder.dup();
    builder.loadLiteral(1);
    invokeMethod(node, getIncDecSelector(operator));
    value.store(builder);
    if (!prefix) builder.pop();
  }

  void visitLocalVariablePrefix(
      SendSet node,
      LocalVariableElement element,
      IncDecOperator operator,
      _) {
    doLocalVariableIncrement(node, element, operator, true);
    applyVisitState();
  }

  void visitParameterPrefix(
      Send node,
      LocalParameterElement parameter,
      IncDecOperator operator,
      _) {
    doLocalVariableIncrement(node, parameter, operator, true);
    applyVisitState();
  }

  void visitLocalVariablePostfix(
      SendSet node,
      LocalVariableElement element,
      IncDecOperator operator,
      _) {
    // If visitState is for effect, we can ignore the return value, thus always
    // generate code for the simpler 'prefix' case.
    bool prefix = (visitState == VisitState.Effect);
    doLocalVariableIncrement(node, element, operator, prefix);
    applyVisitState();
  }

  void visitParameterPostfix(
      SendSet node,
      LocalParameterElement parameter,
      IncDecOperator operator,
      _) {
    // If visitState is for effect, we can ignore the return value, thus always
    // generate code for the simpler 'prefix' case.
    bool prefix = (visitState == VisitState.Effect);
    doLocalVariableIncrement(node, parameter, operator, prefix);
    applyVisitState();
  }

  void doStaticFieldPrefix(
        Node node,
        FieldElement field,
        IncDecOperator operator) {
    doStaticFieldGet(field);
    builder.loadLiteral(1);
    invokeMethod(node, getIncDecSelector(operator));
    doStaticFieldSet(field);
  }

  void doStaticFieldPostfix(
        Node node,
        FieldElement field,
        IncDecOperator operator) {
    doStaticFieldGet(field);
    // For postfix, keep local, unmodified version, to 'return' after store.
    builder.dup();
    builder.loadLiteral(1);
    invokeMethod(node, getIncDecSelector(operator));
    doStaticFieldSet(field);
    builder.pop();
  }

  void visitStaticFieldPostfix(
      Send node,
      FieldElement field,
      IncDecOperator operator,
      _) {
    if (visitState == VisitState.Effect) {
      doStaticFieldPrefix(node, field, operator);
    } else {
      doStaticFieldPostfix(node, field, operator);
    }
    applyVisitState();
  }

  void visitStaticFieldPrefix(
      Send node,
      FieldElement field,
      IncDecOperator operator,
      _) {
    doStaticFieldPrefix(node, field, operator);
    applyVisitState();
  }

  void visitTopLevelFieldPostfix(
      Send node,
      FieldElement field,
      IncDecOperator operator,
      _) {
    if (visitState == VisitState.Effect) {
      doStaticFieldPrefix(node, field, operator);
    } else {
      doStaticFieldPostfix(node, field, operator);
    }
    applyVisitState();
  }

  void visitTopLevelFieldPrefix(
      Send node,
      FieldElement field,
      IncDecOperator operator,
      _) {
    doStaticFieldPrefix(node, field, operator);
    applyVisitState();
  }

  void doDynamicPropertyCompound(
      Node node,
      AssignmentOperator operator,
      Node rhs,
      Selector getterSelector,
      Selector setterSelector) {
    // Dup receiver for setter.
    builder.dup();
    invokeGetter(node, getterSelector);
    visitForValue(rhs);
    invokeMethod(node, getAssignmentSelector(operator));
    invokeSetter(node, setterSelector);
  }

  void visitDynamicPropertyCompound(
      Send node,
      Node receiver,
      AssignmentOperator operator,
      Node rhs,
      Selector getterSelector,
      Selector setterSelector,
      _) {
    visitForValue(receiver);
    doDynamicPropertyCompound(
        node,
        operator,
        rhs,
        getterSelector,
        setterSelector);
    applyVisitState();
  }


  void visitThisPropertyCompound(
      Send node,
      AssignmentOperator operator,
      Node rhs,
      Selector getterSelector,
      Selector setterSelector,
      _) {
    loadThis();
    doDynamicPropertyCompound(
        node,
        operator,
        rhs,
        getterSelector,
        setterSelector);
    applyVisitState();
  }

  void doDynamicPrefix(
      Node node,
      IncDecOperator operator,
      Selector getterSelector,
      Selector setterSelector) {
    builder.dup();
    invokeGetter(node, getterSelector);
    builder.loadLiteral(1);
    invokeMethod(node, getIncDecSelector(operator));
    invokeSetter(node, setterSelector);
  }

  void doIndexPrefix(
      SendSet node,
      Node receiver,
      Node index,
      IncDecOperator operator) {
    visitForValue(receiver);
    visitForValue(index);
    // Load already evaluated receiver and index for '[]' call.
    builder.loadLocal(1);
    builder.loadLocal(1);
    invokeMethod(node, new Selector.index());
    builder.loadLiteral(1);
    invokeMethod(node, getIncDecSelector(operator));
    // Use existing evaluated receiver and index for '[]=' call.
    invokeMethod(node, new Selector.indexSet());
  }

  void visitIndexPrefix(
      SendSet node,
      Node receiver,
      Node index,
      IncDecOperator operator,
      _) {
    doIndexPrefix(node, receiver, index, operator);
    applyVisitState();
  }

  void visitIndexPostfix(
      Send node,
      Node receiver,
      Node index,
      IncDecOperator operator,
      _) {
    if (visitState == VisitState.Effect) {
      doIndexPrefix(node, receiver, index, operator);
      applyVisitState();
      return;
    }

    // Reserve slot for result.
    builder.loadLiteralNull();
    visitForValue(receiver);
    visitForValue(index);
    // Load already evaluated receiver and index for '[]' call.
    builder.loadLocal(1);
    builder.loadLocal(1);
    invokeMethod(node, new Selector.index());
    builder.storeLocal(3);
    builder.loadLiteral(1);
    invokeMethod(node, getIncDecSelector(operator));
    // Use existing evaluated receiver and index for '[]=' call.
    invokeMethod(node, new Selector.indexSet());
    builder.pop();
    applyVisitState();
  }

  void visitCompoundIndexSet(
      Send node,
      Node receiver,
      Node index,
      AssignmentOperator operator,
      Node rhs,
      _) {
    visitForValue(receiver);
    visitForValue(index);
    // Load already evaluated receiver and index for '[]' call.
    builder.loadLocal(1);
    builder.loadLocal(1);
    invokeMethod(node, new Selector.index());
    visitForValue(rhs);
    invokeMethod(node, getAssignmentSelector(operator));
    // Use existing evaluated receiver and index for '[]=' call.
    invokeMethod(node, new Selector.indexSet());
    applyVisitState();
  }

  void visitThisPropertyPrefix(
      Send node,
      IncDecOperator operator,
      Selector getterSelector,
      Selector setterSelector,
      _) {
    loadThis();
    doDynamicPrefix(node, operator, getterSelector, setterSelector);
    applyVisitState();
  }

  void visitThisPropertyPostfix(
      Send node,
      IncDecOperator operator,
      Selector getterSelector,
      Selector setterSelector,
      _) {
    // If visitState is for effect, we can ignore the return value, thus always
    // generate code for the simpler 'prefix' case.
    if (visitState == VisitState.Effect) {
      loadThis();
      doDynamicPrefix(node, operator, getterSelector, setterSelector);
      applyVisitState();
      return;
    }

    loadThis();
    invokeGetter(node, getterSelector);
    // For postfix, keep local, unmodified version, to 'return' after store.
    builder.dup();
    builder.loadLiteral(1);
    invokeMethod(node, getIncDecSelector(operator));
    loadThis();
    builder.loadLocal(1);
    invokeSetter(node, setterSelector);
    builder.popMany(2);
    applyVisitState();
  }

  void visitDynamicPropertyPrefix(
      Send node,
      Node receiver,
      IncDecOperator operator,
      Selector getterSelector,
      Selector setterSelector,
      _) {
    visitForValue(receiver);
    doDynamicPrefix(node, operator, getterSelector, setterSelector);
    applyVisitState();
  }

  void visitDynamicPropertyPostfix(
      Send node,
      Node receiver,
      IncDecOperator operator,
      Selector getterSelector,
      Selector setterSelector,
      _) {
    // If visitState is for effect, we can ignore the return value, thus always
    // generate code for the simpler 'prefix' case.
    if (visitState == VisitState.Effect) {
      visitForValue(receiver);
      doDynamicPrefix(node, operator, getterSelector, setterSelector);
      applyVisitState();
      return;
    }

    int receiverSlot = builder.stackSize;
    visitForValue(receiver);
    builder.loadSlot(receiverSlot);
    invokeGetter(node, getterSelector);
    // For postfix, keep local, unmodified version, to 'return' after store.
    builder.dup();
    builder.loadLiteral(1);
    invokeMethod(node, getIncDecSelector(operator));
    builder.loadSlot(receiverSlot);
    builder.loadLocal(1);
    invokeSetter(node, setterSelector);
    builder.popMany(2);
    builder.storeLocal(1);
    // Pop receiver.
    builder.pop();
    applyVisitState();
  }

  void visitThrow(Throw node) {
    visitForValue(node.expression);
    builder.emitThrow();
    // TODO(ahe): It seems suboptimal that each throw is followed by a pop.
    applyVisitState();
  }

  void visitRethrow(Rethrow node) {
    if (tryBlockStack.isEmpty) {
      doCompileError();
    } else {
      TryBlock block = tryBlockStack.head;
      builder.loadSlot(block.stackSize - 1);
      // TODO(ahe): It seems suboptimal that each throw is followed by a pop.
      builder.emitThrow();
    }
    builder.pop();
  }

  void callConstructor(Node node,
                       ConstructorElement constructor,
                       NodeList arguments,
                       CallStructure callStructure) {
    registerStaticInvocation(constructor);
    registerInstantiatedClass(constructor.enclosingClass);
    CompiledFunction compiledFunction = context.backend.compileConstructor(
        constructor,
        registry);
    doStaticFunctionInvoke(
        node, compiledFunction, arguments, callStructure);
  }

  void visitConstConstructorInvoke(
      NewExpression node,
      ConstructedConstantExpression constant,
      _) {
    int constId = allocateConstantFromNode(node);
    builder.loadConst(constId);
    applyVisitState();
  }

  void errorNonConstantConstructorInvoke(
      NewExpression node,
      Element element,
      DartType type,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    doCompileError();
    applyVisitState();
  }

  void visitGenerativeConstructorInvoke(
      NewExpression node,
      ConstructorElement constructor,
      InterfaceType type,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    if (!checkCompileError(constructor)) {
      if (callStructure.signatureApplies(constructor)) {
        callConstructor(node, constructor, arguments, callStructure);
      } else {
        doUnresolved(constructor.name);
      }
    }
    applyVisitState();
  }

  void visitFactoryConstructorInvoke(
      NewExpression node,
      ConstructorElement constructor,
      InterfaceType type,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    // If the constructor has an implementation, the implementation is the
    // factory we want to invoke. Redirect to
    // visitRedirectingFactoryConstructorInvoke, so we handle both cases of
    // either a factory or a redirecting factory.
    if (constructor.implementation != constructor) {
      ConstructorElement implementation = constructor.implementation;
      visitRedirectingFactoryConstructorInvoke(
          node,
          constructor,
          type,
          implementation.effectiveTarget,
          null,
          arguments,
          callStructure,
          null);
      return;
    }
    // TODO(ahe): Remove ".declaration" when issue 23135 is fixed.
    CompiledFunction compiledFunction =
        requireCompiledFunction(constructor.declaration);
    doStaticFunctionInvoke(
        node, compiledFunction, arguments, callStructure, factoryInvoke: true);
    applyVisitState();
  }

  void visitRedirectingGenerativeConstructorInvoke(
      NewExpression node,
      ConstructorElement constructor,
      InterfaceType type,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    // TODO(ajohnsen): The arguments may need to be shuffled.
    visitGenerativeConstructorInvoke(
        node,
        constructor.effectiveTarget,
        type,
        arguments,
        callStructure,
        null);
 }

  void visitRedirectingFactoryConstructorInvoke(
      NewExpression node,
      ConstructorElement constructor,
      InterfaceType type,
      ConstructorElement effectiveTarget,
      InterfaceType effectiveTargetType,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    if (effectiveTarget.isGenerativeConstructor) {
      visitGenerativeConstructorInvoke(
          node,
          effectiveTarget,
          effectiveTargetType,
          arguments,
          callStructure,
          null);
    } else {
      visitFactoryConstructorInvoke(
          node,
          effectiveTarget,
          effectiveTargetType,
          arguments,
          callStructure,
          null);
    }
  }

  void visitUnresolvedConstructorInvoke(
      NewExpression node,
      Element constructor,
      DartType type,
      NodeList arguments,
      Selector selector,
      _) {
    if (!checkCompileError(constructor.enclosingClass)) {
      doUnresolved(node.send.toString());
    }
    applyVisitState();
  }

  void visitUnresolvedClassConstructorInvoke(
      NewExpression node,
      Element element,
      DartType type,
      NodeList arguments,
      Selector selector,
      _) {
    doUnresolved(node.send.toString());
    applyVisitState();
  }

  void visitAbstractClassConstructorInvoke(
      NewExpression node,
      ConstructorElement element,
      InterfaceType type,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    generateUnimplementedError(node, "Cannot allocate abstract class");
    applyVisitState();
  }

  void errorUnresolvedRedirectingFactoryConstructorInvoke(
      NewExpression node,
      ConstructorElement constructor,
      InterfaceType type,
      NodeList arguments,
      Selector selector,
      _) {
    doUnresolved(node.send.toString());
    applyVisitState();
  }

  void handleStaticGetterGet(Send node, FunctionElement getter, _) {
    if (getter == context.backend.fletchExternalNativeError) {
      builder.loadSlot(0);
      return;
    }
    registerStaticInvocation(getter);
    int methodId = context.backend.functionMethodId(getter);
    int constId = compiledFunction.allocateConstantFromFunction(methodId);
    invokeStatic(node, constId, 0);
    applyVisitState();
  }

  void handleStaticSetterSet(
      Send node,
      FunctionElement setter,
      Node rhs,
      _) {
    visitForValue(rhs);
    registerStaticInvocation(setter);
    int methodId = context.backend.functionMethodId(setter);
    int constId = compiledFunction.allocateConstantFromFunction(methodId);
    invokeStatic(node, constId, 1);
    applyVisitState();
  }

  /**
   * Load the captured variables of [function], expressed in [info].
   *
   * If [function] captures itself, its field index is returned.
   */
  int pushCapturedVariables(FunctionElement function) {
    ClosureInfo info = closureEnvironment.closures[function];
    int index = 0;
    if (info.isThisFree) {
      loadThis();
      index++;
    }
    int thisClosureIndex = -1;
    for (LocalElement element in info.free) {
      if (element == function) {
        // If we capture ourself, remember index and assign into closure after
        // allocation.
        builder.loadLiteralNull();
        assert(thisClosureIndex == -1);
        thisClosureIndex = index;
      } else {
        // Load the raw value (the 'Box' when by reference).
        builder.loadSlot(scope[element].slot);
      }
      index++;
    }
    return thisClosureIndex;
  }

  void visitFunctionExpression(FunctionExpression node) {
    FunctionElement function = elements[node];

    // If the closure captures itself, thisClosureIndex is the field-index in
    // the closure.
    int thisClosureIndex = pushCapturedVariables(function);

    CompiledClass compiledClass = context.backend.createClosureClass(
        function,
        closureEnvironment);
    int classConstant = compiledFunction.allocateConstantFromClass(
        compiledClass.id);
    bool immutable = !closureEnvironment.closures[function].free.any(
        closureEnvironment.shouldBeBoxed);
    builder.allocate(classConstant, compiledClass.fields, immutable: immutable);

    if (thisClosureIndex >= 0) {
      builder.dup();
      builder.storeField(thisClosureIndex);
    }

    registerStaticInvocation(function);
    applyVisitState();
  }

  void visitExpression(Expression node) {
    generateUnimplementedError(
        node, "Missing visit of expression: ${node.runtimeType}");
    applyVisitState();
  }

  void visitStatement(Node node) {
    generateUnimplementedError(
        node, "Missing visit of statement: ${node.runtimeType}");
    builder.pop();
  }

  void doStatements(NodeList statements) {
    List<Element> oldBlockLocals = blockLocals;
    blockLocals = <Element>[];
    int stackSize = builder.stackSize;

    for (Node statement in statements) {
      statement.accept(this);
    }

    int stackSizeDifference = builder.stackSize - stackSize;
    if (stackSizeDifference != blockLocals.length) {
      internalError(
          statements,
          "Unbalanced number of block locals and stack slots used by block.");
    }

    for (int i = blockLocals.length - 1; i >= 0; --i) {
      // TODO(ajohnsen): Pop range bytecode?
      builder.pop();
      popVariableDeclaration(blockLocals[i]);
    }

    blockLocals = oldBlockLocals;
  }

  void visitBlock(Block node) {
    var breakLabel = new BytecodeLabel();
    jumpInfo[node] = new JumpInfo(builder.stackSize, null, breakLabel);
    doStatements(node.statements);
    builder.bind(breakLabel);
  }

  void visitEmptyStatement(EmptyStatement node) {
  }

  void visitExpressionStatement(ExpressionStatement node) {
    visitForEffect(node.expression);
  }

  // Called before 'return', as an option to replace the already evaluated
  // return value.
  // One example is setters.
  void optionalReplaceResultValue() { }

  void visitReturn(Return node) {
    Expression expression = node.expression;
    if (expression == null) {
      builder.loadLiteralNull();
    } else {
      visitForValue(expression);
    }
    callFinallyBlocks(0, true);
    optionalReplaceResultValue();
    generateReturn(node);
  }

  // Find the JumpInfo matching the target of [node].
  JumpInfo getJumpTargetInfo(GotoStatement node) {
    JumpTarget target = elements.getTargetOf(node);
    if (target == null) {
      generateUnimplementedError(node, "'$node' not in loop");
      builder.pop();
      return null;
    }
    Node statement = target.statement;
    JumpInfo info = jumpInfo[statement];
    if (info == null) {
      generateUnimplementedError(node, "'$node' has no target");
      builder.pop();
    }
    return info;
  }

  void callFinallyBlocks(int targetStackSize, bool preserveTop) {
    int popCount = 0;
    for (var block in tryBlockStack) {
      // Break once all exited finally blocks are processed. Finally blocks
      // are ordered by stack size which coincides with scoping. Blocks with
      // stack sizes at least equal to target size are being exited.
      if (block.stackSize < targetStackSize) break;
      if (block.finallyLabel == null) continue;
      if (preserveTop) {
        // We reuse the exception slot as a temporary buffer for the top
        // element, which is located -1 relative to the block's stack size.
        builder.storeSlot(block.stackSize - 1);
      }
      // TODO(ajohnsen): Don't pop, but let subroutineCall take a 'pop count'
      // argument, just like popAndBranch.
      while (builder.stackSize > block.stackSize) {
        builder.pop();
        popCount++;
      }
      builder.subroutineCall(block.finallyLabel, block.finallyReturnLabel);
      if (preserveTop) {
        builder.loadSlot(block.stackSize - 1);
        popCount--;
      }
    }
    // Reallign stack (should be removed, according to above TODO).
    for (int i = 0; i < popCount; i++) {
      // Note we dup, to make sure the top element is the return value.
      builder.dup();
    }
  }

  void unbalancedBranch(GotoStatement node, bool isBreak) {
    JumpInfo info = getJumpTargetInfo(node);
    if (info == null) return;
    callFinallyBlocks(info.stackSize, false);
    BytecodeLabel label = isBreak ? info.breakLabel : info.continueLabel;
    int diff = builder.stackSize - info.stackSize;
    builder.popAndBranch(diff, label);
  }

  void visitBreakStatement(BreakStatement node) {
    var breakLabel = new BytecodeLabel();
    jumpInfo[node] = new JumpInfo(builder.stackSize, null, breakLabel);
    unbalancedBranch(node, true);
    builder.bind(breakLabel);
  }

  void visitContinueStatement(ContinueStatement node) {
    unbalancedBranch(node, false);
  }

  void visitIf(If node) {
    BytecodeLabel ifTrue = new BytecodeLabel();
    BytecodeLabel ifFalse = new BytecodeLabel();
    visitForTest(node.condition, ifTrue, ifFalse);
    builder.bind(ifTrue);
    if (node.hasElsePart) {
      BytecodeLabel end = new BytecodeLabel();
      jumpInfo[node] = new JumpInfo(builder.stackSize, null, end);
      doScopedStatement(node.thenPart);
      builder.branch(end);
      builder.bind(ifFalse);
      doScopedStatement(node.elsePart);
      builder.bind(end);
    } else {
      jumpInfo[node] = new JumpInfo(builder.stackSize, null, ifFalse);
      doScopedStatement(node.thenPart);
      builder.bind(ifFalse);
    }
  }

  void visitFor(For node) {
    List<Element> oldBlockLocals = blockLocals;
    blockLocals = <Element>[];

    BytecodeLabel start = new BytecodeLabel();
    BytecodeLabel ifTrue = new BytecodeLabel();
    BytecodeLabel end = new BytecodeLabel();
    BytecodeLabel afterBody  = new BytecodeLabel();

    int initStackSize = builder.stackSize;
    Node initializer = node.initializer;
    if (initializer != null) visitForEffect(initializer);

    jumpInfo[node] = new JumpInfo(builder.stackSize, afterBody, end);

    builder.bind(start);

    Expression condition = node.condition;
    if (condition != null) {
      visitForTest(condition, ifTrue, end);
      builder.bind(ifTrue);
    }

    doScopedStatement(node.body);

    builder.bind(afterBody);

    for (int i = blockLocals.length - 1; i >= 0; --i) {
      LocalElement local = blockLocals[i];
      // If the locals are captured by reference, load the current value and
      // store it in a new boxed.
      if (closureEnvironment.shouldBeBoxed(local)) {
        LocalValue value = scope[local];
        value.load(builder);
        value.initialize(builder);
        builder.storeSlot(value.slot);
        builder.pop();
      }
    }

    for (Node update in node.update) {
      visitForEffect(update);
    }
    builder.branch(start);

    builder.bind(end);

    for (int i = blockLocals.length - 1; i >= 0; --i) {
      builder.pop();
      popVariableDeclaration(blockLocals[i]);
    }

    blockLocals = oldBlockLocals;
  }

  void visitSyncForIn(SyncForIn node) {
    visitForIn(node);
  }

  void visitForIn(ForIn node) {
    BytecodeLabel start = new BytecodeLabel();
    BytecodeLabel end = new BytecodeLabel();

    // Evalutate expression and iterator.
    visitForValue(node.expression);
    invokeGetter(node.expression, new Selector.getter('iterator', null));

    jumpInfo[node] = new JumpInfo(builder.stackSize, start, end);

    builder.bind(start);

    builder.dup();
    invokeMethod(node, new Selector.call('moveNext', null, 0));
    builder.branchIfFalse(end);

    bool isVariableDeclaration = node.declaredIdentifier.asSend() == null;
    Element element = elements[node];
    if (isVariableDeclaration) {
      // Create local value and load the current element to it.
      LocalValue value = createLocalValueFor(element);
      builder.dup();
      invokeGetter(node, new Selector.getter('current', null));
      value.initialize(builder);
      pushVariableDeclaration(value);
    } else {
      if (element == null || element.isInstanceMember) {
        loadThis();
        builder.loadLocal(1);
        invokeGetter(node, new Selector.getter('current', null));
        Selector selector = elements.getSelector(node.declaredIdentifier);
        invokeSetter(node, selector);
      } else {
        builder.dup();
        invokeGetter(node, new Selector.getter('current', null));
        if (element.isLocal) {
          scope[element].store(builder);
        } else if (element.isField) {
          doStaticFieldSet(element);
        } else if (element.isErroneous) {
          doUnresolved(element.name);
          builder.pop();
        } else {
          internalError(node, "Unhandled store in for-in");
        }
      }
      builder.pop();
    }

    doScopedStatement(node.body);

    if (isVariableDeclaration) {
      // Pop the local again.
      builder.pop();
      popVariableDeclaration(element);
    }

    builder.branch(start);

    builder.bind(end);

    // Pop iterator.
    builder.pop();
  }

  void visitLabeledStatement(LabeledStatement node) {
    node.statement.accept(this);
  }

  // Visit the statement in a scope, where locals are popped when left.
  void doScopedStatement(Node statement) {
    Block block = statement.asBlock();
    if (block != null) {
      doStatements(block.statements);
    } else {
      doStatements(new NodeList.singleton(statement));
    }
  }

  void visitWhile(While node) {
    BytecodeLabel start = new BytecodeLabel();
    BytecodeLabel ifTrue = new BytecodeLabel();
    BytecodeLabel end = new BytecodeLabel();
    jumpInfo[node] = new JumpInfo(builder.stackSize, start, end);
    builder.bind(start);
    visitForTest(node.condition, ifTrue, end);
    builder.bind(ifTrue);
    doScopedStatement(node.body);
    builder.branch(start);
    builder.bind(end);
  }

  void visitDoWhile(DoWhile node) {
    BytecodeLabel start = new BytecodeLabel();
    BytecodeLabel end = new BytecodeLabel();
    BytecodeLabel skipBody = new BytecodeLabel();
    jumpInfo[node] = new JumpInfo(builder.stackSize, skipBody, end);
    builder.bind(start);
    doScopedStatement(node.body);
    builder.bind(skipBody);
    visitForTest(node.condition, start, end);
    builder.bind(end);
  }

  LocalValue initializeLocal(LocalElement element, Expression initializer) {
    int slot = builder.stackSize;
    if (initializer != null) {
      visitForValue(initializer);
    } else {
      builder.loadLiteralNull();
    }
    LocalValue value = createLocalValueFor(element, slot);
    value.initialize(builder);
    pushVariableDeclaration(value);
    blockLocals.add(element);
    return value;
  }

  void visitVariableDefinitions(VariableDefinitions node) {
    for (Node definition in node.definitions) {
      LocalVariableElement element = elements[definition];
      initializeLocal(element, element.initializer);
    }
  }

  void visitFunctionDeclaration(FunctionDeclaration node) {
    FunctionExpression function = node.function;
    initializeLocal(elements[function], function);
  }

  void visitSwitchStatement(SwitchStatement node) {
    BytecodeLabel end = new BytecodeLabel();

    visitForValue(node.expression);

    jumpInfo[node] = new JumpInfo(builder.stackSize, null, end);

    // Install cross-case jump targets.
    for (SwitchCase switchCase in node.cases) {
      BytecodeLabel continueLabel = new BytecodeLabel();
      jumpInfo[switchCase] = new JumpInfo(
          builder.stackSize,
          continueLabel,
          null);
    }

    for (SwitchCase switchCase in node.cases) {
      BytecodeLabel ifTrue = jumpInfo[switchCase].continueLabel;
      BytecodeLabel next = new BytecodeLabel();
      if (!switchCase.isDefaultCase) {
        for (Node labelOrCaseMatch in switchCase.labelsAndCases) {
          CaseMatch caseMatch = labelOrCaseMatch.asCaseMatch();
          if (caseMatch == null) continue;
          generateSwitchCaseMatch(caseMatch, ifTrue);
        }
        builder.branch(next);
      }
      builder.bind(ifTrue);
      doStatements(switchCase.statements);
      builder.branch(end);
      builder.bind(next);
    }

    builder.bind(end);
    builder.pop();
  }

  void doCatchBlock(CatchBlock node, int exceptionSlot, BytecodeLabel end) {
    BytecodeLabel wrongType = new BytecodeLabel();

    TypeAnnotation type = node.type;
    if (type != null) {
      builder.loadSlot(exceptionSlot);
      callIsSelector(type, elements.getType(type), type);
      builder.branchIfFalse(wrongType);
    }

    List<Element> locals = <Element>[];
    Node exception = node.exception;
    if (exception != null) {
      LocalVariableElement element = elements[exception];
      LocalValue value = createLocalValueFor(element);
      builder.loadSlot(exceptionSlot);
      value.initialize(builder);
      pushVariableDeclaration(value);
      locals.add(element);

      Node trace = node.trace;
      if (trace != null) {
        LocalVariableElement element = elements[trace];
        LocalValue value = createLocalValueFor(element);
        builder.loadLiteralNull();
        value.initialize(builder);
        pushVariableDeclaration(value);
        // TODO(ajohnsen): Set trace.
        locals.add(element);
      }
    }

    node.block.accept(this);

    builder.popMany(locals.length);
    for (Element e in locals) {
      popVariableDeclaration(e);
    }

    builder.branch(end);

    builder.bind(wrongType);
  }

  void visitTryStatement(TryStatement node) {
    BytecodeLabel end = new BytecodeLabel();
    BytecodeLabel finallyLabel = new BytecodeLabel();
    BytecodeLabel finallyReturnLabel = new BytecodeLabel();

    Block finallyBlock = node.finallyBlock;
    bool hasFinally = finallyBlock != null;

    // Reserve slot for exception.
    int exceptionSlot = builder.stackSize;
    builder.loadLiteralNull();

    jumpInfo[node] = new JumpInfo(builder.stackSize, null, end);

    int startBytecodeSize = builder.byteSize;

    tryBlockStack = tryBlockStack.prepend(
        new TryBlock(
            builder.stackSize,
            hasFinally ? finallyLabel : null,
            hasFinally ? finallyReturnLabel: null));

    node.tryBlock.accept(this);

    // Go to end if no exceptions was thrown.
    builder.branch(end);
    int endBytecodeSize = builder.byteSize;

    // Add catch-frame to the builder.
    builder.addCatchFrameRange(startBytecodeSize, endBytecodeSize);

    for (Node catchBlock in node.catchBlocks) {
      doCatchBlock(catchBlock, exceptionSlot, end);
    }

    tryBlockStack = tryBlockStack.tail;

    if (hasFinally) {
      if (!node.catchBlocks.isEmpty) {
        builder.addCatchFrameRange(endBytecodeSize, builder.byteSize);
      }
      // Catch exception from catch blocks.
      builder.subroutineCall(finallyLabel, finallyReturnLabel);
    }

    // The exception was not cought. Rethrow.
    builder.emitThrow();

    builder.bind(end);

    if (hasFinally) {
      BytecodeLabel done = new BytecodeLabel();
      builder.subroutineCall(finallyLabel, finallyReturnLabel);
      builder.branch(done);

      builder.bind(finallyLabel);
      builder.applyStackSizeFix(1);
      finallyBlock.accept(this);
      builder.subroutineReturn(finallyReturnLabel);

      builder.bind(done);
    }

    // Pop exception slot.
    builder.pop();
  }

  void doUnresolved(String name) {
    var constString = context.backend.constantSystem.createString(
        new DartString.literal(name));
    context.markConstantUsed(constString);
    builder.loadConst(compiledFunction.allocateConstant(constString));
    FunctionElement function = context.backend.fletchUnresolved;
    registerStaticInvocation(function);
    int methodId = context.backend.functionMethodId(function);
    int constId = compiledFunction.allocateConstantFromFunction(methodId);
    builder.invokeStatic(constId, 1);
  }

  bool checkCompileError(Element element) {
    if (context.compiler.elementsWithCompileTimeErrors.contains(element)) {
      doCompileError();
      return true;
    }
    return false;
  }

  void doCompileError() {
    FunctionElement function = context.backend.fletchCompileError;
    registerStaticInvocation(function);
    int methodId = context.backend.functionMethodId(function);
    int constId = compiledFunction.allocateConstantFromFunction(methodId);
    builder.invokeStatic(constId, 0);
  }

  void visitUnresolvedInvoke(
      Send node,
      Element element,
      Node arguments,
      Selector selector,
      _) {
    if (!checkCompileError(element)) {
      doUnresolved(node.selector.toString());
    }
    applyVisitState();
  }

  void visitUnresolvedGet(
      Send node,
      Element element,
      _) {
    doUnresolved(node.selector.toString());
    applyVisitState();
  }

  void visitUnresolvedSet(
      Send node,
      Element element,
      Node rhs,
      _) {
    doUnresolved(node.selector.toString());
    applyVisitState();
  }

  void handleStaticFunctionIncompatibleInvoke(
      Send node,
      MethodElement function,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    if (!checkCompileError(function)) {
      doUnresolved(function.name);
    }
    applyVisitState();
  }

  void internalError(Spannable spannable, String reason) {
    context.compiler.internalError(spannable, reason);
  }

  void generateUnimplementedError(Spannable spannable, String reason) {
    context.backend.generateUnimplementedError(
        spannable,
        reason,
        compiledFunction);
  }

  String toString() => "FunctionCompiler(${element.name})";

  void handleFinalStaticFieldSet(
      SendSet node,
      FieldElement field,
      Node rhs,
      _) {
    generateUnimplementedError(
        node, "[handleFinalStaticFieldSet] isn't implemented.");
    applyVisitState();
  }

  void handleImmutableLocalSet(
      SendSet node,
      LocalElement element,
      Node rhs,
      _) {
    generateUnimplementedError(
        node, "[handleImmutableLocalSet] isn't implemented.");
    applyVisitState();
  }

  void handleStaticSetterGet(
      SendSet node,
      FunctionElement setter,
      _) {
    generateUnimplementedError(
        node, "[handleStaticSetterGet] isn't implemented.");
    applyVisitState();
  }

  void handleStaticSetterInvoke(
      Send node,
      FunctionElement setter,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    generateUnimplementedError(
        node, "[handleStaticSetterInvoke] isn't implemented.");
    applyVisitState();
  }

  void handleStaticGetterSet(
      Send node,
      FunctionElement getter,
      Node rhs,
      _) {
    generateUnimplementedError(
        node, "[handleStaticGetterSet] isn't implemented.");
    applyVisitState();
  }

  void handleStaticGetterInvoke(
      Send node,
      FunctionElement getter,
      NodeList arguments,
      CallStructure callStructure,
      _) {
    generateUnimplementedError(
        node, "[handleStaticGetterInvoke] isn't implemented.");
    applyVisitState();
  }

  void handleStaticFunctionSet(
      SendSet node,
      MethodElement function,
      Node rhs,
      _) {
    generateUnimplementedError(
        node, "[handleStaticFunctionSet] isn't implemented.");
    applyVisitState();
  }

  void bulkHandleNode(Node node, String msg, _) {
    generateUnimplementedError(node, msg.replaceAll('#', node.toString()));
    applyVisitState();
  }

  void visitNode(Node node) {
    internalError(node, "[visitNode] isn't implemented.");
  }

  void apply(Node node, _) {
    internalError(node, "[apply] isn't implemented.");
  }

  void applyInitializers(NodeList initializers, _) {
    internalError(node, "[applyInitializers] isn't implemented.");
  }

  void applyParameters(NodeList parameters, _) {
    internalError(node, "[applyParameters] isn't implemented.");
  }
}
