// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "include/fletch_api.h"

namespace fletch {

static bool IsSnapshot(uint8_t* snapshot, uint32_t length) {
  return length > 2 && snapshot[0] == 0xbe && snapshot[1] == 0xef;
}

static int Main(int argc, char** argv) {
  printf("running main\n");
  uint8_t* bytes = static_cast<uint8_t*>(malloc(512));
  FletchSetup();
  FletchRunSnapshot(bytes, 512);
  FletchTearDown();
  free(bytes);
  return 0;
}

}  // namespace fletch


// Forward main calls to fletch::Main.
int main(int argc, char** argv) {
  return fletch::Main(argc, argv);
}
