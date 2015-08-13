#!/bin/bash
set -e

# Build the config.h file required for stm32.
( cd ../third_party/lk/ &&
  make PROJECT=stm32746g-eval2-test build-stm32746g-eval2-test/config.h )

# Now build libfletch.a.
ninja -C out/ReleaseXLKARM/ libfletch.a

# And finally build the LK image, with libfletch.a linked into it.
( cd ../third_party/lk/ &&
  make PROJECT=stm32746g-eval2-test)
