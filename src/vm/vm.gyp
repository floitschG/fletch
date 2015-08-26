# Copyright (c) 2015, the Fletch project authors. Please see the AUTHORS file
# for details. All rights reserved. Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE.md file.

{
  'target_defaults': {
    'include_dirs': [
      '../../',
    ],
  },
  'targets': [
    {
      'target_name': 'fletch_vm_library_base',
      'type': 'static_library',
      'toolsets': ['target', 'host'],
      'dependencies': [
        '../shared/shared.gyp:fletch_shared',
        '../double_conversion.gyp:double_conversion',
      ],
      'sources': [
        # TODO(ahe): Add header (.h) files.
        'debug_info.cc',
        'event_handler.cc',
        'event_handler_macos.cc',
        'event_handler_linux.cc',
        'ffi.h',
        'ffi.cc',
        'ffi_linux.cc',
        'ffi_macos.cc',
        'ffi_posix.cc',
        'ffi_disabled.cc',
        'fletch.cc',
        'fletch_api_impl.cc',
        'heap.cc',
        'heap_validator.cc',
        'immutable_heap.cc',
        'gc_thread.cc',
        'interpreter.cc',
        'intrinsics.cc',
        'lookup_cache.cc',
        'natives.cc',
        'object.cc',
        'object_list.cc',
        'object_map.cc',
        'object_memory.cc',
        'port.cc',
        'process.cc',
        'program.cc',
        'program_folder.cc',
        'scheduler.cc',
        'selector_row.cc',
        'service_api_impl.cc',
        'session.cc',
        'snapshot.cc',
        'stack_walker.cc',
        'storebuffer.cc',
        'thread_pool.cc',
        'thread_posix.cc',
        'unicode.cc',
        'weak_pointer.cc',
      ],
      'link_settings': {
        'libraries': [
          '-ltcmalloc_minimal',
          '-lpthread',
          '-ldl',
          # TODO(ahe): Not sure this option works as intended on Mac.
          '-rdynamic',
        ],
      },
    },
    {
      'target_name': 'fletch_vm_library',
      'type': 'static_library',
      'dependencies': [
        'fletch_vm_library_generator#host',
        'fletch_vm_library_base',
        '../shared/shared.gyp:fletch_shared',
        '../double_conversion.gyp:double_conversion',
      ],

      # TODO(kasperl): Remove the below conditions when we no longer use weak
      # symbols.
      'conditions': [
        [ 'OS=="linux"', {
          'link_settings': {
            'ldflags': [
              '-Wl,--whole-archive',
            ],
            'libraries': [
              '-Wl,--no-whole-archive',
            ],
          },
        }],
      ],

      'sources': [
        '<(INTERMEDIATE_DIR)/generated.S',
      ],
      'actions': [
        {
          'action_name': 'generate_generated_S',
          'inputs': [
            '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)'
            'fletch_vm_library_generator'
            '<(EXECUTABLE_SUFFIX)',
          ],
          'outputs': [
            '<(INTERMEDIATE_DIR)/generated.S',
          ],
          'action': [
            # TODO(ahe): Change generator to accept command line argument for
            # output file. Using file redirection may not work well on Windows.
            'bash', '-c', '<(_inputs) > <(_outputs)',
          ],
        },
      ],
    },
    {
      'target_name': 'libfletch',
      'type': 'none',
      'dependencies': [
        'fletch_vm_library',
        'fletch_vm_library_base',
        '../shared/shared.gyp:fletch_shared',
        '../double_conversion.gyp:double_conversion',
      ],
      'sources': [
        # TODO(ager): Lint target default requires a source file. Not
        # sure how to work around that.
        'assembler.h',
      ],
      'actions': [
        {
          'action_name': 'generate_libfletch',
          'conditions': [
            [ 'OS=="linux"', {
              'inputs': [
                '../../tools/library_combiner.py',
                '<(PRODUCT_DIR)/obj/src/vm/libfletch_vm_library.a',
                '<(PRODUCT_DIR)/obj/src/vm/libfletch_vm_library_base.a',
                '<(PRODUCT_DIR)/obj/src/shared/libfletch_shared.a',
                '<(PRODUCT_DIR)/obj/src/libdouble_conversion.a',
              ],
            }],
            [ 'OS=="mac"', {
              'inputs': [
                '../../tools/library_combiner.py',
                '<(PRODUCT_DIR)/libfletch_vm_library.a',
                '<(PRODUCT_DIR)/libfletch_vm_library_base.a',
                '<(PRODUCT_DIR)/libfletch_shared.a',
                '<(PRODUCT_DIR)/libdouble_conversion.a',
              ],
            }],
          ],
          'outputs': [
            '<(PRODUCT_DIR)/libfletch.a',
          ],
          'action': [
            'bash', '-c', 'python <(_inputs) <(_outputs)',
          ]
        },
      ]
    },
    {
      'target_name': 'fletch_vm_library_generator',
      'type': 'executable',
      'toolsets': ['host'],
      'dependencies': [
        '../shared/shared.gyp:fletch_shared',
        '../double_conversion.gyp:double_conversion',
      ],
      'sources': [
        # TODO(ahe): Add header (.h) files.
        'assembler_arm64_linux.cc',
        'assembler_arm64_macos.cc',
        'assembler_arm.cc',
        'assembler_arm_linux.cc',
        'assembler_arm_macos.cc',
        'assembler_x64.cc',
        'assembler_x64_linux.cc',
        'assembler_x64_macos.cc',
        'assembler_x86.cc',
        'assembler_x86_linux.cc',
        'assembler_x86_macos.cc',
        'generator.cc',
        'interpreter_arm.cc',
        'interpreter_x86.cc',
      ],
    },
    {
      'target_name': 'fletch-vm',
      'type': 'executable',
      'dependencies': [
        'libfletch',
      ],
      'sources': [
        # TODO(ahe): Add header (.h) files.
        'main.cc',
      ],
    },
    {
      'target_name': 'vm_cc_tests',
      'type': 'executable',
      'dependencies': [
        'libfletch',
      ],
      'defines': [
        'TESTING',
        # TODO(ahe): Remove this when GYP is the default.
        'GYP',
      ],
      'sources': [
        # TODO(ahe): Add header (.h) files.
        'object_map_test.cc',
        'object_memory_test.cc',
        'object_test.cc',
        'platform_test.cc',

        '../shared/test_main.cc',
      ],
    },
    {
      'target_name': 'ffi_test_library',
      'type': 'shared_library',
      'dependencies': [
      ],
      'sources': [
        'ffi_test_library.c',
      ],
    },
  ],
}
