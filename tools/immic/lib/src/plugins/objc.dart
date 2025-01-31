// Copyright (c) 2015, the Fletch project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library servicec.plugins.cc;

import 'dart:core' hide Type;
import 'dart:io' show Platform, File;

import 'package:path/path.dart' show basenameWithoutExtension, join, dirname;

import 'shared.dart';

import '../emitter.dart';
import '../primitives.dart' as primitives;
import '../struct_layout.dart';

const List<String> RESOURCES = const [
  "ImmiBase.h",
];

const COPYRIGHT = """
// Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.
""";

const Map<String, String> _TYPES = const {
  'void'    : 'void',
  'bool'    : 'bool',

  'uint8'   : 'uint8_t',
  'uint16'  : 'uint16_t',

  'int8'    : 'int8_t',
  'int16'   : 'int16_t',
  'int32'   : 'int32_t',
  'int64'   : 'int64_t',

  'float32' : 'float',
  'float64' : 'double',

  'String' : 'NSString*',
};

String getTypePointer(Type type) {
  if (type.isNode) return 'Node*';
  if (type.resolved != null) {
    return "${type.identifier}Node*";
  }
  return _TYPES[type.identifier];
}

String getTypeName(Type type) {
  if (type.isNode) return 'Node';
  if (type.resolved != null) {
    return "${type.identifier}Node";
  }
  return _TYPES[type.identifier];
}

void generate(String path, Map units, String outputDirectory) {
  String directory = join(outputDirectory, "objc");
  _generateHeaderFile(path, units, directory);
  _generateImplementationFile(path, units, directory);

  String resourcesDirectory = join(dirname(Platform.script.path),
      '..', 'lib', 'src', 'resources', 'objc');
  for (String resource in RESOURCES) {
    String resourcePath = join(resourcesDirectory, resource);
    File file = new File(resourcePath);
    String contents = file.readAsStringSync();
    writeToFile(directory, resource, contents);
  }
}

void _generateHeaderFile(String path, Map units, String directory) {
  _HeaderVisitor visitor = new _HeaderVisitor(path);
  visitor.visitUnits(units);
  String contents = visitor.buffer.toString();
  String file = visitor.immiImplFile;
  writeToFile(directory, file, contents, extension: 'h');
}

void _generateImplementationFile(String path, Map units, String directory) {
  _ImplementationVisitor visitor = new _ImplementationVisitor(path);
  visitor.visitUnits(units);
  String contents = visitor.buffer.toString();
  String file = visitor.immiImplFile;
  writeToFile(directory, file, contents, extension: 'mm');
}

  String getName(node) {
    if (node is Struct) return node.name;
    if (node is String) return node;
    throw 'Invalid arg';
  }

  String applyToMethodName(node) {
    return 'applyTo';
  }

  String presentMethodName(node) {
    String name = getName(node);
    return 'present${name}';
  }

  String patchMethodName(node) {
    String name = getName(node);
    return 'patch${name}';
  }

  String applyToMethodSignature(node) {
    String name = getName(node);
    return '- (void)${applyToMethodName(name)}:(id <${name}Presenter>)presenter';
  }

  String presentMethodSignature(node) {
    String name = getName(node);
    String type = name == 'Node' ? name : '${name}Node';
    return '- (void)${presentMethodName(name)}:(${type}*)node';
  }

  String patchMethodSignature(node) {
    String name = getName(node);
    return '- (void)${patchMethodName(name)}:(${name}Patch*)patch';
  }

  String applyToMethodDeclaration(Struct node) {
    return applyToMethodSignature(node) + ';';
  }

  String presentMethodDeclaration(Struct node) {
    return presentMethodSignature(node) + ';';
  }

  String patchMethodDeclaration(Struct node) {
    return patchMethodSignature(node) + ';';
  }


abstract class _ObjCVisitor extends CodeGenerationVisitor {
  _ObjCVisitor(String path) : super(path);

  String immiBaseFile = 'ImmiBase.h';
  String immiImplFile = 'Immi.h';
}

class _HeaderVisitor extends _ObjCVisitor {
  _HeaderVisitor(String path) : super(path);

  List nodes = [];

  visitUnits(Map units) {
    units.values.forEach(collectMethodSignatures);
    units.values.forEach((unit) { nodes.addAll(unit.structs); });
    _writeHeader();
    nodes.forEach((node) { writeln('@class ${node.name}Node;'); });
    writeln();
    nodes.forEach((node) { writeln('@class ${node.name}Patch;'); });
    writeln();
    _writeActions();
    units.values.forEach(visit);
  }

  visitUnit(Unit unit) {
    unit.structs.forEach(visit);
  }

  visitStruct(Struct node) {
    StructLayout layout = node.layout;
    String nodeName = "${node.name}Node";
    String nodeNameData = "${nodeName}Data";
    String patchName = "${node.name}Patch";
    String patchNameData = "${nodeName}PatchData";
    String presenterName = "${node.name}Presenter";
    writeln('@protocol $presenterName');
    writeln(presentMethodDeclaration(node));
    writeln(patchMethodDeclaration(node));
    writeln('@end');
    writeln();
    writeln('@interface $nodeName : NSObject <Node>');
    forEachSlot(node, null, (Type slotType, String slotName) {
      write('@property (readonly) ');
      _writeNSType(slotType);
      writeln(' $slotName;');
    });
    for (var method in node.methods) {
      List<Type> formalTypes = method.arguments.map((formal) => formal.type);
      String actionBlock = 'Action${actionTypeSuffix(formalTypes)}Block';
      writeln('@property (readonly) $actionBlock ${method.name};');
    }
    writeln('@end');
    writeln();
    writeln('@interface $patchName : NSObject <NodePatch>');
    writeln('@property (readonly) bool changed;');
    writeln('@property (readonly) $nodeName* previous;');
    writeln('@property (readonly) $nodeName* current;');
    forEachSlot(node, null, (Type slotType, String slotName) {
      writeln('@property (readonly) ${patchType(slotType)}* $slotName;');
    });
    for (var method in node.methods) {
      String actionPatch = actionPatchType(method);
      writeln('@property (readonly) $actionPatch* ${method.name};');
    }
    writeln(applyToMethodDeclaration(node));
    writeln('@end');
    writeln();
  }

  void _writeFormalWithKeyword(String keyword, Formal formal) {
    write('$keyword:(${getTypeName(formal.type)})${formal.name}');
  }

  _writeActions() {
    for (List<Type> formals in methodSignatures.values) {
      String actionName = 'Action${actionTypeSuffix(formals)}';
      String actionBlock = '${actionName}Block';
      String actionPatch = '${actionName}Patch';
      write('typedef void (^$actionBlock)(${actionTypeFormals(formals)});');
      writeln();
      writeln('@interface $actionPatch : NSObject <Patch>');
      writeln('@property (readonly) $actionBlock current;');
      writeln('@end');
      writeln();
    }
  }

  void _writeNSType(Type type) {
    if (type.isList) {
      write('NSArray*');
    } else {
      write(getTypePointer(type));
    }
  }

  void _writeHeader() {
    writeln(COPYRIGHT);
    writeln('// Generated file. Do not edit.');
    writeln();
    writeln('#import "$immiBaseFile"');
    writeln();
  }

  String patchType(Type type) {
    if (type.isList) return 'ListPatch';
    return '${camelize(type.identifier)}Patch';
  }

  String actionTypeSuffix(List<Type> types) {
    if (types.isEmpty) return 'Void';
    return types.map((Type type) => camelize(type.identifier)).join();
  }

  String actionTypeFormals(List<Type> types) {
    return types.map((Type type) => getTypeName(type)).join(', ');
  }

  String actionPatchType(Method method) {
    List<Type> types = method.arguments.map((formal) => formal.type);
    return 'Action${actionTypeSuffix(types)}Patch';
  }

  String actionBlockType(Method method) {
    List<Type> types = method.arguments.map((formal) => formal.type);
    return 'Action${actionTypeSuffix(types)}Block';
  }
}

class _ImplementationVisitor extends _ObjCVisitor {
  _ImplementationVisitor(String path) : super(path);

  List<Struct> nodes = [];

  visitUnits(Map units) {
    units.values.forEach(collectMethodSignatures);
    units.values.forEach((unit) { nodes.addAll(unit.structs); });
    _writeHeader();

    _writeNodeBaseExtendedInterface();
    _writePatchBaseExtendedInterface();
    _writePatchPrimitivesExtendedInterface();
    _writeActionsExtendedInterface();
    units.values.forEach((unit) {
      unit.structs.forEach(_writeNodeExtendedInterface);
    });

    _writeImmiServiceExtendedInterface();
    _writeImmiRootExtendedInterface();

    _writeEventUtils();
    _writeStringUtils();
    _writeListUtils();

    _writeNodeBaseImplementation();
    _writePatchBaseImplementation();
    _writePatchPrimitivesImplementation();
    _writeActionsImplementation();
    units.values.forEach((unit) {
      unit.structs.forEach(_writeNodeImplementation);
    });

    _writeImmiServiceImplementation();
    _writeImmiRootImplementation();
  }

  visitUnit(Unit unit) {
    // Everything is done in visitUnits.
  }

  _writeImmiServiceExtendedInterface() {
    writeln('@interface ImmiService ()');
    writeln('@property NSMutableArray* storyboards;');
    writeln('@property NSMutableDictionary* roots;');
    writeln('@end');
    writeln();
  }

  _writeImmiServiceImplementation() {
    writeln('@implementation ImmiService');
    writeln();
    writeln('- (id)init {');
    writeln('  self = [super init];');
    writeln('  _storyboards = [NSMutableArray array];');
    writeln('  _roots = [NSMutableDictionary dictionary];');
    writeln('  return self;');
    writeln('}');
    writeln();
    writeln('- (ImmiRoot*)registerPresenter:(id <NodePresenter>)presenter');
    writeln('                       forName:(NSString*)name {');
    writeln('  assert(self.roots[name] == nil);');
    writeln('  int length = name.length;');
    writeln('  int size = 48 + PresenterDataBuilder::kSize + length;');
    writeln('  MessageBuilder message(size);');
    writeln('  PresenterDataBuilder builder =');
    writeln('      message.initRoot<PresenterDataBuilder>();');
    writeln('  List<unichar> chars = builder.initNameData(length);');
    writeln('  [name getCharacters:chars.data()');
    writeln('                range:NSMakeRange(0, length)];');
    writeln('  uint16_t pid = ImmiServiceLayer::getPresenter(builder);');
    writeln('  ImmiRoot* root =');
    writeln('     [[ImmiRoot alloc] init:pid presenter:presenter];');
    writeln('  self.roots[name] = root;');
    writeln('  return root;');
    writeln('}');
    writeln();
    writeln('- (void)registerStoryboard:(UIStoryboard*)storyboard {');
    writeln('  [self.storyboards addObject:storyboard];');
    writeln('}');
    writeln();
    writeln('- (ImmiRoot*)getRootByName:(NSString*)name {');
    writeln('  ImmiRoot* root = self.roots[name];');
    writeln('  if (root != nil) return root;');
    writeln('  id <NodePresenter> presenter = nil;');
    writeln('  for (int i = 0; i < self.storyboards.count; ++i) {');
    writeln('    @try {');
    writeln('      presenter = [self.storyboards[i]');
    writeln('          instantiateViewControllerWithIdentifier:name];');
    writeln('      break;');
    writeln('    }');
    writeln('    @catch (NSException* e) {');
    writeln('      if (e.name != NSInvalidArgumentException) {');
    writeln('        @throw e;');
    writeln('      }');
    writeln('    }');
    writeln('  }');
    writeln('  if (presenter == nil) abort();');
    writeln('  return [self registerPresenter:presenter forName:name];');
    writeln('}');
    writeln();
    writeln('- (id <NodePresenter>)getPresenterByName:(NSString*)name {');
    writeln('  return [[self getRootByName:name] presenter];');
    writeln('}');
    writeln();
    writeln('@end');
    writeln();
  }

  _writeImmiRootExtendedInterface() {
    writeln('@interface ImmiRoot ()');
    writeln('@property (readonly) uint16_t pid;');
    writeln('@property (readonly) id <NodePresenter> presenter;');
    writeln('@property Node* previous;');
    writeln('@property bool refreshPending;');
    writeln('@property bool refreshRequired;');
    writeln('@property (nonatomic) dispatch_queue_t refreshQueue;');
    writeln('- (id)init:(uint16_t)pid presenter:(id <NodePresenter>)presenter;');
    writeln('@end');
    writeln();
  }

  _writeImmiRootImplementation() {
    writeln('typedef void (^ImmiRefreshCallback)(const PatchData&);');
    writeln('void ImmiRefresh(PatchData patchData, void* callbackData) {');
    writeln('  @autoreleasepool {');
    writeln('    ImmiRefreshCallback block =');
    writeln('        (__bridge_transfer ImmiRefreshCallback)callbackData;');
    writeln('    block(patchData);');
    writeln('    patchData.Delete();');
    writeln('  }');
    writeln('}');
    writeln('@implementation ImmiRoot');
    writeln();
    writeln('- (id)init:(uint16_t)pid');
    writeln('    presenter:(id <NodePresenter>)presenter {');
    writeln('  self = [super init];');
    writeln('  assert(pid > 0);');
    writeln('  _pid = pid;');
    writeln('  _presenter = presenter;');
    writeln('  _refreshPending = false;');
    writeln('  _refreshRequired = false;');
    writeln('  _refreshQueue = dispatch_queue_create(');
    writeln('      "com.google.immi.refreshQueue", DISPATCH_QUEUE_SERIAL);');
    writeln('  return self;');
    writeln('}');
    writeln();
    writeln('- (void)refresh {');
    writeln('  ImmiRefreshCallback doApply = ^(const PatchData& patchData) {');
    writeln('      if (patchData.isNode()) {');
    writeln('        NodePatch* patch = [[NodePatch alloc]');
    writeln('            initWith:patchData.getNode()');
    writeln('            previous:self.previous');
    writeln('             inGraph:self];');
    writeln('        self.previous = patch.current;');
    writeln('        [patch applyTo:self.presenter];');
    writeln('      }');
    writeln('      dispatch_async(self.refreshQueue, ^{');
    writeln('          if (self.refreshRequired) {');
    writeln('            self.refreshRequired = false;');
    writeln('            [self refresh];');
    writeln('          } else {');
    writeln('            self.refreshPending = false;');
    writeln('          }');
    writeln('      });');
    writeln('  };');
    writeln('  ${serviceName}::refreshAsync(');
    writeln('      self.pid,');
    writeln('      ImmiRefresh,');
    writeln('      (__bridge_retained void*)[doApply copy]);');
    writeln('}');
    writeln();
    writeln('- (void)reset {');
    writeln('  ${serviceName}::reset(self.pid);');
    writeln('}');
    writeln();
    writeln('- (void)dispatch:(ImmiDispatchBlock)block {');
    writeln('  block();');
    writeln('  [self requestRefresh];');
    writeln('}');
    writeln();
    writeln('- (void)requestRefresh {');
    writeln('  dispatch_async(self.refreshQueue, ^{');
    writeln('      if (self.refreshPending) {');
    writeln('        self.refreshRequired = true;');
    writeln('      } else {');
    writeln('        self.refreshPending = true;');
    writeln('        [self refresh];');
    writeln('      }');
    writeln('  });');
    writeln('}');
    writeln();
    writeln('@end');
    writeln();
  }

  _writeNodeExtendedInterface(Struct node) {
    String name = node.name;
    String nodeName = "${name}Node";
    String patchName = "${name}Patch";
    String nodeDataName = "${nodeName}Data";
    String patchDataName = "${patchName}Data";
    writeln('@interface $nodeName ()');
    if (node.methods.isNotEmpty) {
      writeln('@property (weak) ImmiRoot* root;');
    }
    writeln('- (id)initWith:(const $nodeDataName&)data');
    writeln('       inGraph:(ImmiRoot*)root;');
    writeln('- (id)initWithPatch:($patchName*)patch;');
    writeln('@end');
    writeln();
    writeln('@interface $patchName ()');
    writeln('- (id)initIdentityPatch:($nodeName*)previous;');
    writeln('- (id)initWith:(const $patchDataName&)data');
    writeln('      previous:($nodeName*)previous');
    writeln('       inGraph:(ImmiRoot*)root;');
    writeln('@end');
    writeln();
  }

  _writeNodeImplementation(Struct node) {
    String name = node.name;
    String nodeName = "${node.name}Node";
    String patchName = "${node.name}Patch";
    String nodeDataName = "${nodeName}Data";
    String patchDataName = "${patchName}Data";
    String updateDataName = "${node.name}UpdateData";
    writeln('@implementation $nodeName');
    writeln('- (id)initWith:(const $nodeDataName&)data');
    writeln('       inGraph:(ImmiRoot*)root {');
    writeln('  self = [super init];');
    forEachSlot(node, null, (Type slotType, String slotName) {
      String camelName = camelize(slotName);
      write('  _$slotName = ');
      if (slotType.isList) {
        String slotTypeName = getTypeName(slotType.elementType.isNode ?
                                          slotType.elementType :
                                          slotType);
        String slotTypeData = "${slotTypeName}Data";
        writeln('ListUtils<$slotTypeData>::decodeList(');
        writeln('      data.get${camelName}(), create$slotTypeName, root);');
      } else if (slotType.isString) {
        writeln('decodeString(data.get${camelName}Data());');
      } else if (slotType.isNode || slotType.resolved != null) {
        String slotTypeName = getTypeName(slotType);
        writeln('[[$slotTypeName alloc] initWith:data.get${camelName}()');
        writeln('                        inGraph:(ImmiRoot*)root];');
      } else {
        writeln('data.get${camelName}();');
      }
    });
    for (var method in node.methods) {
      String actionId = '${method.name}Id';
      writeln('  uint16_t $actionId = data.get${camelize(method.name)}();');
      write('  _${method.name} = ');
      List<Type> formals = method.arguments.map((formal) => formal.type);
      _writeActionBlockImplementation(actionId, formals);
      writeln(';');
    }
    writeln('  return self;');
    writeln('}');

    writeln('- (id)initWithPatch:($patchName*)patch {');
    writeln('  self = [super init];');
    forEachSlot(node, null, (Type slotType, String slotName) {
      writeln('  _$slotName = patch.$slotName.current;');
    });
    for (Method method in node.methods) {
      String name = method.name;
      writeln('  _$name = patch.$name.current;');
    }
    writeln('  return self;');
    writeln('}');
    writeln('@end');
    writeln();
    writeln('@implementation $patchName {');
    writeln('  NodePatchType _type;');
    writeln('}');
    writeln('- (id)initIdentityPatch:($nodeName*)previous {');
    writeln('  self = [super init];');
    writeln('  _type = kIdentityNodePatch;');
    writeln('  _previous = previous;');
    writeln('  _current = previous;');
    writeln('  return self;');
    writeln('}');
    writeln('- (id)initWith:(const $patchDataName&)data');
    writeln('      previous:($nodeName*)previous');
    writeln('       inGraph:(ImmiRoot*)root {');
    writeln('  self = [super init];');
    writeln('  _previous = previous;');
    if (node.layout.slots.isNotEmpty || node.methods.isNotEmpty) {
      // The updates list is ordered consistently with the struct fields.
      writeln('  if (data.isUpdates()) {');
      writeln('    List<$updateDataName> updates = data.getUpdates();');
      writeln('    int length = updates.length();');
      writeln('    int next = 0;');
      forEachSlotAndMethod(node, null, (field, String name) {
        String camelName = camelize(name);
        String fieldPatchType =
            field is Method ? actionPatchType(field) : patchType(field.type);
        writeln('    if (next < length && updates[next].is${camelName}()) {');
        writeln('      _$name = [[$fieldPatchType alloc]');
        write('                      ');
        String dataGetter = 'updates[next++].get${camelName}';
        if (field is Formal && !field.type.isList && field.type.isString) {
          writeln('initWith:decodeString(${dataGetter}Data())');
        } else {
          writeln('initWith:$dataGetter()');
        }
        writeln('                      previous:previous.$name');
        writeln('                       inGraph:root];');
        writeln('    } else {');
        writeln('      _$name = [[$fieldPatchType alloc]');
        writeln('                    initIdentityPatch:previous.$name];');
        writeln('    }');
      });
      writeln('    assert(next == length);');
      writeln('    _type = kUpdateNodePatch;');
      writeln('    _current = [[$nodeName alloc] initWithPatch:self];');
      writeln('    return self;');
      writeln('  }');
    }
    writeln('  assert(data.isReplace());');
    writeln('  _type = kReplaceNodePatch;');
    writeln('  _current = [[$nodeName alloc] initWith:data.getReplace()');
    writeln('                                 inGraph:root];');
    // For replace patches we leave fields and methods as default initialized.
    writeln('  return self;');
    writeln('}');

    writeln('- (bool)changed { return _type != kIdentityNodePatch; }');
    writeln('- (bool)replaced { return _type == kReplaceNodePatch; }');
    writeln('- (bool)updated { return _type == kUpdateNodePatch; }');
    write(applyToMethodSignature(node));
    writeln(' {');
    writeln('  if (!self.changed) return;');
    writeln('  if (self.replaced) {');
    writeln('    [presenter present${node.name}:self.current];');
    writeln('  } else {');
    writeln('    [presenter patch${node.name}:self];');
    writeln('  }');
    writeln('}');
    writeln('@end');
    writeln();
  }

  void _writeNodeBaseExtendedInterface() {
    writeln('@interface Node ()');
    writeln('@property (readonly) id <Node> node;');
    writeln('- (id)init:(id <Node>)node;');
    writeln('- (id)initWith:(const NodeData&)data');
    writeln('       inGraph:(ImmiRoot*)root;');
    writeln('+ (id <Node>)createNode:(const NodeData&)data');
    writeln('                inGraph:(ImmiRoot*)root;');
    writeln('@end');
    writeln();
  }

  void _writeNodeBaseImplementation() {
    writeln('@implementation Node');
    writeln('- (bool)is:(Class)klass {');
    writeln('  return [self.node isMemberOfClass:klass];');
    writeln('}');
    writeln('- (id)as:(Class)klass {');
    writeln('  assert([self is:klass]);');
    writeln('  return self.node;');
    writeln('}');
    writeln('- (id)init:(id <Node>)node {');
    writeln('  self = [super init];');
    writeln('  _node = node;');
    writeln('  return self;');
    writeln('}');
    writeln('- (id)initWith:(const NodeData&)data');
    writeln('       inGraph:(ImmiRoot*)root {');
    writeln('  return [self init:[Node createNode:data inGraph:root]];');
    writeln('}');
    writeln('+ (id <Node>)createNode:(const NodeData&)data');
    writeln('                inGraph:(ImmiRoot*)root {');
    nodes.forEach((node) {
      writeln('  if (data.is${node.name}()) {');
      writeln('    return [[${node.name}Node alloc]');
      writeln('            initWith:data.get${node.name}()');
      writeln('             inGraph:root];');
      writeln('  }');
    });
    writeln('  abort();');
    writeln('}');
    writeln('@end');
    writeln();
  }

  void _writePatchBaseExtendedInterface() {
    writeln('typedef enum { kIdentityNodePatch, kReplaceNodePatch, kUpdateNodePatch } NodePatchType;');
    writeln('@interface NodePatch ()');
    writeln('@property (readonly) id <NodePatch> patch;');
    writeln('@property (readonly) Node* node;');
    writeln('- (id)initIdentityPatch:(Node*)previous;');
    writeln('- (id)initWith:(const NodePatchData&)data');
    writeln('      previous:(Node*)previous');
    writeln('       inGraph:(ImmiRoot*)root;');
    writeln('+ (id <NodePatch>)createPatch:(const NodePatchData&)data');
    writeln('                     previous:(id <Node>)previous');
    writeln('                      inGraph:(ImmiRoot*)root;');
    writeln('@end');
    writeln();
  }

  void _writePatchBaseImplementation() {
    writeln('@implementation NodePatch {');
    writeln('  NodePatchType _type;');
    writeln('}');
    writeln('+ (id <NodePatch>)createPatch:(const NodePatchData&)data');
    writeln('                     previous:(id <Node>)previous');
    writeln('                      inGraph:(ImmiRoot*)root {');
    nodes.forEach((node) {
      String name = node.name;
      String nodeName = '${name}Node';
      String patchName = '${name}Patch';
      writeln('  if (data.is$name()) {');
      writeln('    $nodeName* previousNode =');
      writeln('      [previous isMemberOfClass:$nodeName.class] ?');
      writeln('          previous :');
      writeln('          nil;');
      writeln('    return [[$patchName alloc]');
      writeln('            initWith:data.get$name()');
      writeln('            previous:previousNode');
      writeln('             inGraph:root];');
      writeln('  }');
    });
    writeln('  abort();');
    writeln('}');
    writeln('- (id)initIdentityPatch:(Node*)previous {');
    writeln('  self = [super init];');
    writeln('  _type = kIdentityNodePatch;');
    writeln('  _previous = previous;');
    writeln('  _current = previous;');
    writeln('  return self;');
    writeln('}');
    writeln('- (id)initWith:(const NodePatchData&)data');
    writeln('      previous:(Node*)previous');
    writeln('       inGraph:(ImmiRoot*)root {');
    writeln('  self = [super init];');
    writeln('  _previous = previous;');
    writeln('  _patch = [NodePatch');
    writeln('            createPatch:data');
    writeln('               previous:previous.node');
    writeln('                inGraph:root];');
    writeln('  _type = _patch.replaced ? kReplaceNodePatch : kUpdateNodePatch;');
    writeln('  _current = [[Node alloc] init:_patch.current];');
    writeln('  return self;');
    writeln('}');
    writeln();
    writeln('- (bool)changed { return _type != kIdentityNodePatch; }');
    writeln('- (bool)replaced { return _type == kReplaceNodePatch; }');
    writeln('- (bool)updated { return _type == kUpdateNodePatch; }');
    writeln();
    write(applyToMethodSignature('Node'));
    writeln(' {');
    writeln('  if (!self.changed) return;');
    writeln('  if (self.replaced) {');
    writeln('    [presenter presentNode:self.current];');
    writeln('  } else {');
    writeln('    [presenter patchNode:self];');
    writeln('  }');
    writeln('}');
    writeln('- (bool)is:(Class)klass {');
    writeln('  return [self.patch isMemberOfClass:klass];');
    writeln('}');
    writeln('- (id)as:(Class)klass {');
    writeln('  assert([self is:klass]);');
    writeln('  return self.patch;');
    writeln('}');
    writeln('@end');
    writeln();
  }

  void _writePatchPrimitivesExtendedInterface() {
    _TYPES.forEach((String idlType, String objcType) {
      if (idlType == 'void') return;
      String patchTypeName = '${camelize(idlType)}Patch';
      String patchDataName = objcType;
      writeln('@interface $patchTypeName ()');
      writeln('- (id)initIdentityPatch:($objcType)previous;');
      writeln('- (id)initWith:($patchDataName)data');
      writeln('      previous:($objcType)previous');
      writeln('       inGraph:(ImmiRoot*)root;');
      writeln('@end');
      writeln();
    });

    writeln('typedef enum { kAnyNode, kSpecificNode } ListPatchType;');
    writeln();
    writeln('@interface ListRegionPatch ()');
    writeln('@property (readonly) int countDelta;');
    writeln('- (int)applyTo:(NSMutableArray*)outArray');
    writeln('          with:(NSArray*)inArray;');
    writeln('+ (ListRegionPatch*)regionPatch:(const ListRegionData&)data');
    writeln('                           type:(ListPatchType)type');
    writeln('                       previous:(NSArray*)previous');
    writeln('                        inGraph:(ImmiRoot*)root;');
    writeln('@end');
    writeln('@interface ListRegionRemovePatch ()');
    writeln('- (id)initWith:(const ListRegionData&)data');
    writeln('          type:(ListPatchType)type');
    writeln('       inGraph:(ImmiRoot*)root;');
    writeln('@end');
    writeln('@interface ListRegionInsertPatch ()');
    writeln('- (id)initWith:(const ListRegionData&)data');
    writeln('          type:(ListPatchType)type');
    writeln('       inGraph:(ImmiRoot*)root;');
    writeln('@end');
    writeln('@interface ListRegionUpdatePatch ()');
    writeln('- (id)initWith:(const ListRegionData&)data');
    writeln('          type:(ListPatchType)type');
    writeln('      previous:(NSArray*)previous');
    writeln('       inGraph:(ImmiRoot*)root;');
    writeln('@end');

    writeln('@interface ListPatch ()');
    writeln('- (id)initIdentityPatch:(NSArray*)previous;');
    writeln('- (id)initWith:(const ListPatchData&)data');
    writeln('      previous:(NSArray*)previous');
    writeln('       inGraph:(ImmiRoot*)root;');
    writeln('@end');
    writeln();
  }

  void _writePatchPrimitivesImplementation() {
    _TYPES.forEach((String idlType, String objcType) {
      if (idlType == 'void') return;
      String patchTypeName = '${camelize(idlType)}Patch';
      String patchDataName = objcType;
      writeln('@implementation $patchTypeName');
      writeln('- (id)initIdentityPatch:($objcType)previous {');
      writeln('  self = [super init];');
      writeln('  _previous = previous;');
      writeln('  _current = previous;');
      writeln('  return self;');
      writeln('}');
      writeln('- (id)initWith:($patchDataName)data');
      writeln('      previous:($objcType)previous');
      writeln('       inGraph:(ImmiRoot*)root {');
      writeln('  self = [super init];');
      writeln('  _previous = previous;');
      writeln('  _current = data;');
      writeln('  return self;');
      writeln('}');
      writeln('- (bool)changed {');
      writeln('  return _previous != _current;');
      writeln('}');
      writeln('@end');
      writeln();
    });

    writeln('@implementation ListRegionPatch');
    writeln('+ (ListRegionPatch*)regionPatch:(const ListRegionData&)data');
    writeln('                           type:(ListPatchType)type');
    writeln('                       previous:(NSArray*)previous');
    writeln('                        inGraph:(ImmiRoot*)root {');
    writeln('  if (data.isRemove()) {');
    writeln('    return [[ListRegionRemovePatch alloc] initWith:data');
    writeln('                                              type:type');
    writeln('                                           inGraph:root];');
    writeln('  }');
    writeln('  if (data.isInsert()) {');
    writeln('    return [[ListRegionInsertPatch alloc] initWith:data');
    writeln('                                              type:type');
    writeln('                                           inGraph:root];');
    writeln('  }');
    writeln('  NSAssert(data.isUpdate(), @"Invalid list patch for region");');
    writeln('  return [[ListRegionUpdatePatch alloc] initWith:data');
    writeln('                                            type:type');
    writeln('                                        previous:previous');
    writeln('                                         inGraph:root];');
    writeln('}');
    writeln('- (id)init:(int)index {');
    writeln('  self = [super init];');
    writeln('  _index = index;');
    writeln('  return self;');
    writeln('}');
    writeln('- (bool)isRemove { return false; }');
    writeln('- (bool)isInsert { return false; }');
    writeln('- (bool)isUpdate { return false; }');
    writeln('- (int)applyTo:(NSMutableArray*)outArray');
    writeln('          with:(NSArray*)inArray {');
    writeln('    @throw [NSException');
    writeln('        exceptionWithName:NSInternalInconsistencyException');
    writeln('        reason:@"-applyTo:with: base is abstract"');
    writeln('        userInfo:nil];');
    writeln('}');
    writeln('@end');
    writeln();
    writeln('@implementation ListRegionRemovePatch');
    writeln('- (id)initWith:(const ListRegionData&)data');
    writeln('          type:(ListPatchType)type');
    writeln('       inGraph:(ImmiRoot*)root {');
    writeln('  self = [super init:data.getIndex()];');
    writeln('  _count = data.getRemove();');
    writeln('  return self;');
    writeln('}');
    writeln('- (bool)isRemove { return true; }');
    writeln('- (int)countDelta { return -self.count; }');
    writeln('- (int)applyTo:(NSMutableArray*)outArray');
    writeln('          with:(NSArray*)inArray {');
    writeln('  return self.count;');
    writeln('}');
    writeln('@end');
    writeln();
    writeln('@implementation ListRegionInsertPatch');
    writeln('- (id)initWith:(const ListRegionData&)data');
    writeln('          type:(ListPatchType)type');
    writeln('       inGraph:(ImmiRoot*)root {');
    writeln('  self = [super init:data.getIndex()];');
    writeln('  const List<NodeData>& insertData = data.getInsert();');
    writeln('  NSMutableArray* nodes =');
    writeln('      [NSMutableArray arrayWithCapacity:insertData.length()];');
    writeln('  int length = insertData.length();');
    writeln('  if (type == kAnyNode) {');
    writeln('    for (int i = 0; i < length; ++i) {');
    writeln('      nodes[i] = [[Node alloc] initWith:insertData[i]');
    writeln('                                inGraph:root];');
    writeln('    }');
    writeln('  } else {');
    writeln('    assert(type == kSpecificNode);');
    writeln('    for (int i = 0; i < length; ++i) {');
    writeln('      nodes[i] = [Node createNode:insertData[i] inGraph:root];');
    writeln('    }');
    writeln('  }');
    writeln('  _nodes = nodes;');
    writeln('  return self;');
    writeln('}');
    writeln('- (bool)isInsert { return true; }');
    writeln('- (int)countDelta { return self.nodes.count; }');
    writeln('- (int)applyTo:(NSMutableArray*)outArray');
    writeln('          with:(NSArray*)inArray {');
    writeln('  [outArray addObjectsFromArray:self.nodes];');
    writeln('  return 0;');
    writeln('}');
    writeln('@end');
    writeln();
    writeln('@implementation ListRegionUpdatePatch');
    writeln('- (id)initWith:(const ListRegionData&)data');
    writeln('          type:(ListPatchType)type');
    writeln('      previous:(NSArray*)previous');
    writeln('       inGraph:(ImmiRoot*)root {');
    writeln('  self = [super init:data.getIndex()];');
    writeln('  const List<NodePatchData>& updateData = data.getUpdate();');
    writeln('  int length = updateData.length();');
    writeln('  NSMutableArray* updates =');
    writeln('      [NSMutableArray arrayWithCapacity:length];');
    writeln('  if (type == kAnyNode) {');
    writeln('    for (int i = 0; i < length; ++i) {');
    writeln('      updates[i] = [[NodePatch alloc]');
    writeln('          initWith:updateData[i]');
    writeln('          previous:previous[self.index + i]');
    writeln('           inGraph:root];');
    writeln('    }');
    writeln('  } else {');
    writeln('    assert(type == kSpecificNode);');
    writeln('    for (int i = 0; i < length; ++i) {');
    writeln('      updates[i] = [NodePatch');
    writeln('          createPatch:updateData[i]');
    writeln('             previous:previous[self.index + i]');
    writeln('              inGraph:root];');
    writeln('    }');
    writeln('  }');
    writeln('  _updates = updates;');
    writeln('  return self;');
    writeln('}');
    writeln('- (bool)isUpdate { return true; }');
    writeln('- (int)countDelta { return 0; }');
    writeln('- (int)applyTo:(NSMutableArray*)outArray');
    writeln('          with:(NSArray*)inArray {');
    writeln('  for (int i = 0; i < self.updates.count; ++i) {');
    writeln('    id <NodePatch> patch = self.updates[i];');
    writeln('    [outArray addObject:patch.current];');
    writeln('  }');
    writeln('  return self.updates.count;');
    writeln('}');
    writeln('@end');
    writeln();
    writeln('@implementation ListPatch {');
    writeln('  NSMutableArray* _regions;');
    writeln('}');
    writeln('- (id)initIdentityPatch:(NSArray*)previous {');
    writeln('  self = [super init];');
    writeln('  _changed = false;');
    writeln('  _previous = previous;');
    writeln('  _current = previous;');
    writeln('  return self;');
    writeln('}');
    writeln('- (id)initWith:(const ListPatchData&)data');
    writeln('      previous:(NSArray*)previous');
    writeln('       inGraph:(ImmiRoot*)root {');
    writeln('  self = [super init];');
    writeln('  _changed = true;');
    writeln('  _previous = previous;');
    writeln('  ListPatchType type = (ListPatchType)data.getType();');
    writeln('  const List<ListRegionData>& regions = data.getRegions();');
    writeln('  NSMutableArray* patches =');
    writeln('      [NSMutableArray arrayWithCapacity:regions.length()];');
    writeln('  for (int i = 0; i < regions.length(); ++i) {');
    writeln('    patches[i] =');
    writeln('        [ListRegionPatch regionPatch:regions[i]');
    writeln('                                type:type');
    writeln('                            previous:previous');
    writeln('                             inGraph:root];');
    writeln('  }');
    writeln('  _regions = patches;');
    writeln('  _current = [self applyWith:previous];');
    writeln('  return self;');
    writeln('}');
    writeln('- (NSArray*)applyWith:(NSArray*)array {');
    writeln('  int newCount = array.count;');
    writeln('  for (int i = 0; i < self.regions.count; ++i) {');
    writeln('    ListRegionPatch* patch = self.regions[i];');
    writeln('    newCount += patch.countDelta;');
    writeln('  }');
    writeln('  int sourceIndex = 0;');
    writeln('  NSMutableArray* newArray =');
    writeln('      [NSMutableArray arrayWithCapacity:newCount];');
    writeln('  for (int i = 0; i < self.regions.count; ++i) {');
    writeln('    ListRegionPatch* patch = self.regions[i];');
    writeln('    while (sourceIndex < patch.index) {');
    writeln('      [newArray addObject:array[sourceIndex++]];');
    writeln('    }');
    writeln('    sourceIndex += [patch applyTo:newArray with:array];');
    writeln('  }');
    writeln('  while (sourceIndex < array.count) {');
    writeln('    [newArray addObject:array[sourceIndex++]];');
    writeln('  }');
    writeln('  return newArray;');
    writeln('}');
    writeln('@end');
    writeln();
  }

  void _writeActionsExtendedInterface() {
    for (List<Type> formals in methodSignatures.values) {
      String actionName = 'Action${actionTypeSuffix(formals)}';
      String actionBlock = '${actionName}Block';
      String actionPatch = '${actionName}Patch';
      writeln('@interface $actionPatch ()');
      writeln('- (id)initIdentityPatch:($actionBlock)previous;');
      writeln('- (id)initWith:(uint16_t)actionId');
      writeln('      previous:($actionBlock)previous');
      writeln('       inGraph:(ImmiRoot*)root;');
      writeln('@end');
      writeln();
    }
  }

  void _writeActionsImplementation() {
    for (List<Type> formals in methodSignatures.values) {
      String suffix = actionTypeSuffix(formals);
      String actionName = 'Action$suffix';
      String actionBlock = '${actionName}Block';
      String actionPatch = '${actionName}Patch';

      writeln('@implementation $actionPatch {');
      writeln('  NodePatchType _type;');
      writeln('}');
      writeln('- (id)initIdentityPatch:($actionBlock)previous {');
      writeln('  self = [super init];');
      writeln('  _type = kIdentityNodePatch;');
      writeln('  _current = previous;');
      writeln('  return self;');
      writeln('}');
      writeln('- (id)initWith:(uint16_t)actionId');
      writeln('      previous:($actionBlock)previous');
      writeln('       inGraph:(ImmiRoot*)root {');
      writeln('  self = [super init];');
      writeln('  _type = kReplaceNodePatch;');
      write('  _current = ');
      _writeActionBlockImplementation('actionId', formals);
      writeln(';');
      writeln('  return self;');
      writeln('}');
      writeln('- (bool)changed { return _type != kIdentityNodePatch; }');
      writeln('@end');
      writeln();
    }
  }

  void _writeActionBlockImplementation(String actionId, List<Type> formals) {
      String suffix = actionTypeSuffix(formals);
      bool boxedArguments = formals.any((t) => t.isString);
      String actionFormals = '';
      if (formals.isNotEmpty) {
        var typedFormals =
          mapWithIndex(formals, (i, f) => '${getTypeName(f)} arg$i').join(', ');
        actionFormals = '(${typedFormals})';
      }
      writeln('^$actionFormals{');
      writeln('      [root dispatch:^{');
      if (boxedArguments) {
        writeln('          int size = 48 + Action${suffix}ArgsBuilder::kSize;');
        int i = 0;
        for (Type formal in formals) {
          if (formal.isString) {
            writeln('          size += arg$i.length;');
          }
          i++;
        }
        writeln('          MessageBuilder message(size);');
        writeln('          Action${suffix}ArgsBuilder args =');
        writeln('            message.initRoot<Action${suffix}ArgsBuilder>();');
        writeln('          args.setId($actionId);');
        i = 0;
        for (Type formal in formals) {
          if (formal.isString) {
            String charBuffer = 'args.initArg${i}Data(arg$i.length).data()';
            writeln('          [arg$i');
            writeln('           getCharacters:$charBuffer');
            writeln('                   range:NSMakeRange(0, arg$i.length)];');
          } else {
            writeln('          args.setArg$i(arg$i);');
          }
          i++;
        }
      }
      writeln('          ${serviceName}::dispatch${suffix}Async(');
      if (boxedArguments) {
        writeln('              args,');
      } else {
        writeln('              $actionId,');
        for (int i = 0; i < formals.length; ++i) {
          writeln('              arg$i,');
        }
      }
      writeln('              noopVoidEventCallback,');
      writeln('              NULL);');
      writeln('      }];');
      write('  }');
  }

  void _writeListUtils() {
    nodes.forEach((Struct node) {
      String name = node.name;
      String nodeName = "${node.name}Node";
      String patchName = "${node.name}Patch";
      String nodeDataName = "${nodeName}Data";
      String patchDataName = "${patchName}Data";
      writeln('id create$nodeName(const $nodeDataName& data, ImmiRoot* root) {');
      writeln('  return [[$nodeName alloc] initWith:data inGraph:root];');
      writeln('}');
      writeln();
    });
    // TODO(zerny): Support lists of primitive types.
    writeln("""
id createNode(const NodeData& data, ImmiRoot* root) {
  return [Node createNode:data inGraph:root];
}

template<typename T>
class ListUtils {
public:
  typedef id (*DecodeElementFunction)(const T&, ImmiRoot*);

  static NSMutableArray* decodeList(const List<T>& list,
                                    DecodeElementFunction decodeElement,
                                    ImmiRoot* root) {
    NSMutableArray* array = [NSMutableArray arrayWithCapacity:list.length()];
    for (int i = 0; i < list.length(); ++i) {
      [array addObject:decodeElement(list[i], root)];
    }
    return array;
  }
};
""");
  }

  void _writeStringUtils() {
    write("""
NSString* decodeString(const List<unichar>& chars) {
  List<unichar>& tmp = const_cast<List<unichar>&>(chars);
  return [[NSString alloc] initWithCharacters:tmp.data()
                                       length:tmp.length()];
}

void encodeString(NSString* string, List<unichar> chars) {
  assert(string.length == chars.length());
  [string getCharacters:chars.data()
                  range:NSMakeRange(0, string.length)];
}

""");
  }

  void _writeEventUtils() {
    writeln('typedef uint16_t EventID;');
    writeln('void noopVoidEventCallback(void*) {}');
    writeln();
  }

  void _writeHeader() {
    String fileName = basenameWithoutExtension(path);
    writeln(COPYRIGHT);
    writeln('// Generated file. Do not edit.');
    writeln();
    writeln('#import "$immiImplFile"');
    writeln();
  }

  void _writeFormalWithKeyword(String keyword, Formal formal) {
    write('$keyword:(${getTypeName(formal.type)})${formal.name}');
  }

  String patchType(Type type) {
    if (type.isList) return 'ListPatch';
    return '${camelize(type.identifier)}Patch';
  }

  void _writeNSType(Type type) {
    if (type.isList) {
      write('NSArray*');
    } else {
      write(getTypePointer(type));
    }
  }

  String actionFormalTypes(List<Type> types) {
    return types.map((Type type) => getTypeName(type)).join(', ');
  }

  String actionTypedArguments(List<Formal> types) {
    return types.map((Formal formal) {
      return '${getTypeName(formal.type)} ${formal.name}';
    }).join(', ');
  }

  String actionArguments(List<Formal> types) {
    return types.map((Formal formal) {
      return '${formal.name}';
    }).join(', ');
  }

  String actionPatchType(Method method) {
    List<Type> types = method.arguments.map((formal) => formal.type);
    return 'Action${actionTypeSuffix(types)}Patch';
  }

  String actionBlockType(Method method) {
    List<Type> types = method.arguments.map((formal) => formal.type);
    return 'Action${actionTypeSuffix(types)}Block';
  }
}
