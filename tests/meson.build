test_c_args = ['-D_DEFAULT_SOURCE', '-D_XOPEN_SOURCE=600', '-DNDEBUG']
test_sources = ['../src/metadata.c', '../src/utils.c']

test_metadata = executable('test_metadata',
  ['test_metadata.c'] + test_sources,
  dependencies: [cmocka_dep],
  include_directories: inc,
  c_args: test_c_args,
)

test_utils = executable('test_utils',
  ['test_utils.c'] + test_sources,
  dependencies: [cmocka_dep],
  include_directories: inc,
  c_args: test_c_args,
)

test_config = executable('test_config',
  ['test_config.c', '../src/config.c'],
  dependencies: [cmocka_dep],
  include_directories: inc,
  c_args: test_c_args,
)

test('metadata parsing', test_metadata)
test('utility functions', test_utils)
test('config functions', test_config)

test_art = executable('test_art',
  ['test_art.c', '../lib/trim.c'],
  dependencies: [cmocka_dep],
  include_directories: inc,
  c_args: test_c_args,
)

test('art trimming functions', test_art)
