test_c_args = ['-D_DEFAULT_SOURCE', '-D_XOPEN_SOURCE=600', '-DNDEBUG']
test_sources = ['../src/metadata.c', '../src/utils.c', '../src/config.c']
test_deps = [cmocka_dep, chafa]

test_metadata = executable('test_metadata',
  ['test_metadata.c'] + test_sources,
  dependencies: test_deps,
  include_directories: inc,
  c_args: test_c_args,
)

test_utils = executable('test_utils',
  ['test_utils.c'] + test_sources,
  dependencies: test_deps,
  include_directories: inc,
  c_args: test_c_args,
)

test_config = executable('test_config',
  ['test_config.c'] + test_sources,
  dependencies: test_deps,
  include_directories: inc,
  c_args: test_c_args,
)

test('metadata parsing', test_metadata)
test('utility functions', test_utils)
test('config functions', test_config)

test_art = executable('test_art',
  ['test_art.c', '../lib/trim.c'],
  dependencies: test_deps,
  include_directories: inc,
  c_args: test_c_args,
)

test('art trimming functions', test_art)

cache_deps = [cmocka_dep, chafa, libavformat, libavcodec, libswscale, libavutil, m_dep]
cache_sources = ['../src/art.c', '../src/art_manager.c', '../src/metadata.c', '../src/utils.c', '../src/config.c', '../lib/img2ascii.c', '../lib/trim.c']

test_art_cache = executable('test_art_cache',
  ['test_art_cache.c'] + cache_sources,
  dependencies: cache_deps,
  include_directories: inc,
  c_args: test_c_args,
)

test('art cache functions', test_art_cache)
