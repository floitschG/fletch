// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "include/fletch_api.h"

extern "C" {
int RunSnapshotFromEmscripten(uint8_t* data, int length) {
  printf("running snapshot (size %d)\n", length);
  printf("%x %x %x %x %x\n", data[0], data[1], data[2], data[3], data[4]);
  FletchSetup();
  FletchRunSnapshot(data, length);
  FletchTearDown();
  return 0;
}
}  // namespace emscripten
