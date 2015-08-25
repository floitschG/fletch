// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

library fletchc.fletch_system;

import 'package:compiler/src/elements/elements.dart' show
    ClassElement,
    Element,
    FieldElement,
    FunctionSignature;

import 'package:persistent/persistent.dart' show
    PersistentMap;

import 'bytecodes.dart';
import 'commands.dart';

enum FletchFunctionKind {
  NORMAL,
  LAZY_FIELD_INITIALIZER,
  INITIALIZER_LIST,
  PARAMETER_STUB,
  ACCESSOR
}

// TODO(ajohnsen): Move to separate file.
class FletchConstant {
  final int id;
  final MapId mapId;
  const FletchConstant(this.id, this.mapId);

  String toString() => "FletchConstant($id, $mapId)";
}

// TODO(ajohnsen): Move to separate file.
class FletchClass {
  final int classId;
  final String name;
  final ClassElement element;
  final int superclassId;
  final int superclassFields;
  final PersistentMap<int, int> methodTable;
  final List<FieldElement> fields;

  const FletchClass(
      this.classId,
      this.name,
      this.element,
      this.superclassId,
      this.superclassFields,
      this.methodTable,
      this.fields);

  bool get hasSuperclassId => superclassId >= 0;

  String toString() => "FletchClass($classId, '$name')";
}

// TODO(ajohnsen): Move to separate file.
class FletchFunctionBase {
  final int functionId;
  final FletchFunctionKind kind;
  // TODO(ajohnsen): Merge with function signature?
  final int arity;
  // TODO(ajohnsen): Remove name?
  final String name;
  final Element element;

  /**
   * The signature of the FletchFunctionBuilder.
   *
   * Some compiled functions does not have a signature (for example, generated
   * accessors).
   */
  final FunctionSignature signature;
  final int memberOf;

  const FletchFunctionBase(
      this.functionId,
      this.kind,
      this.arity,
      this.name,
      this.element,
      this.signature,
      this.memberOf);

  bool get isInstanceMember => memberOf != null;
  bool get isInternal => element == null;

  bool get isLazyFieldInitializer {
    return kind == FletchFunctionKind.LAZY_FIELD_INITIALIZER;
  }

  bool get isInitializerList {
    return kind == FletchFunctionKind.INITIALIZER_LIST;
  }

  bool get isAccessor {
    return kind == FletchFunctionKind.ACCESSOR;
  }

  bool get isParameterStub {
    return kind == FletchFunctionKind.PARAMETER_STUB;
  }

  bool get isConstructor => element != null && element.isConstructor;
}

// TODO(ajohnsen): Move to separate file.
class FletchFunction extends FletchFunctionBase {
  final List<Bytecode> bytecodes;
  final List<FletchConstant> constants;

  const FletchFunction(
      int functionId,
      FletchFunctionKind kind,
      int arity,
      String name,
      Element element,
      FunctionSignature signature,
      this.bytecodes,
      this.constants,
      int memberOf)
      : super(functionId, kind, arity, name, element, signature, memberOf);

  FletchFunction withReplacedConstants(List<FletchConstant> constants) {
    return new FletchFunction(
        functionId,
        kind,
        arity,
        name,
        element,
        signature,
        bytecodes,
        constants,
        memberOf);
  }

  /// Represents a function we have lost track off, for example, -1 in a
  /// backtrace from the Fletch VM.
  const FletchFunction.missing()
    : this(
        -1, FletchFunctionKind.NORMAL, 0, "<missing>", null, null,
        const <Bytecode>[], const <FletchConstant>[], null);

  String toString() {
    StringBuffer buffer = new StringBuffer();
    buffer.write("FletchFunction($functionId, '$name'");
    if (isInstanceMember) {
      buffer.write(", memberOf=$memberOf");
    }
    buffer.write(")");
    return buffer.toString();
  }
}

class FletchSystem {
  // functionsByElement is a subset of functionsById: Some functions do not
  // have an element reference.
  final PersistentMap<int, FletchFunction> functionsById;
  final PersistentMap<Element, FletchFunction> functionsByElement;

  // classesByElement is a subset of classesById: Some classes do not
  // have an element reference.
  final PersistentMap<int, FletchClass> classesById;
  final PersistentMap<ClassElement, FletchClass> classesByElement;

  // TODO(ajohnsen): Should it be a map?
  final List<FletchConstant> constants;

  const FletchSystem(
      this.functionsById,
      this.functionsByElement,
      this.classesById,
      this.classesByElement,
      this.constants);

  bool get isEmpty => functionsById.isEmpty;

  FletchFunction lookupFunctionById(int functionId) {
    return functionsById[functionId];
  }

  FletchFunction lookupFunctionByElement(Element element) {
    return functionsByElement[element];
  }

  Iterable<FletchFunction> functionsWhere(bool f(FletchFunction function)) {
    return functionsById.values.where(f);
  }

  FletchClass lookupClassById(int classId) {
    return classesById[classId];
  }

  FletchClass lookupClassByElement(ClassElement element) {
    return classesByElement[element];
  }

  int computeMaxFunctionId() {
    return functionsById.keys.fold(-1, (x, y) => x > y ? x : y);
  }

  int computeMaxClassId() {
    return classesById.keys.fold(-1, (x, y) => x > y ? x : y);
  }
}

class FletchDelta {
  final FletchSystem system;
  final FletchSystem predecessorSystem;
  final List<Command> commands;

  const FletchDelta(this.system, this.predecessorSystem, this.commands);
}
