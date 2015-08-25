// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "include/fletch_api.h"

#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/var_array_buffer.h"

namespace fletch {

static bool IsSnapshot(uint8_t* snapshot, uint32_t length) {
  return length > 2 && snapshot[0] == 0xbe && snapshot[1] == 0xef;
}

class FletchInstance : public pp::Instance {
 public:
  explicit FletchInstance(PP_Instance instance) : pp::Instance(instance) {
    printf("instantiated fletch instance\n");
  }
  virtual ~FletchInstance() {}

  /// Handler for messages coming in from the browser via postMessage().  The
  /// var_message can contain any pp:Var type; for example int, string
  /// Array or Dictionary.
  /// For Fletch we expect a byte-buffer. The module will crash for anything
  /// else.
  virtual void HandleMessage(const pp::Var& var_message) {
    printf("received message from JavaScript\n");
    if (var_message.is_array_buffer()) {
      pp::VarArrayBuffer::VarArrayBuffer buffer(var_message);
      uint32_t length = buffer.ByteLength();
      void* buffer_data = buffer.Map();
      uint8_t* data = static_cast<uint8_t*>(malloc(length));
      memcpy(data, buffer_data, length);
      if (IsSnapshot(data, length)) {
        printf("Executing snapshot (%d bytes)\n", length);
        FletchSetup();
        FletchRunSnapshot(data, length);
        FletchTearDown();
        return;
      }
    }

    pp::Var var_reply("Not a Snapshot");
    PostMessage(var_reply);
  }
};

/// The Module class.  The browser calls the CreateInstance() method to create
/// an instance of your NaCl module on the web page.  The browser creates a new
/// instance for each <embed> tag with type="application/x-pnacl".
class FletchModule : public pp::Module {
 public:
  FletchModule() : pp::Module() {}
  virtual ~FletchModule() {}

  /// Create and return a FletchInstance object.
  /// The argument `instance` is the browser-side instance.
  /// Returns the plugin-side instance.
  virtual pp::Instance* CreateInstance(PP_Instance instance) {
    return new FletchInstance(instance);
  }
};


}  // namespace fletch

namespace pp {
/// Factory function called by the browser when the module is first loaded.
/// The browser keeps a singleton of this module.  It calls the
/// CreateInstance() method on the object you return to make instances.  There
/// is one instance per <embed> tag on the page.  This is the main binding
/// point for your NaCl module with the browser.
Module* CreateModule() {
  return new fletch::FletchModule();
}

}  // namespace pp
