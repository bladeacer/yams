project('yams', 'c',
  version : '1.0.0',
  default_options : [
    'c_std=c11',
    'warning_level=3',
    'buildtype=debugoptimized',
    'optimization=s'
  ]
)

ncursesw = dependency('ncursesw')

panelw = dependency('panelw', required : false)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

if not panelw.found()
  panelw = cc.find_library('panelw', required : true)
endif

libavformat = dependency('libavformat', required : true)
libavcodec = dependency('libavcodec', required : true)
libswscale = dependency('libswscale', required : true)
libavutil = dependency('libavutil', required : true)

chafa = dependency('chafa', required : true, version : '>=1.16')

if not get_option('logging')
  add_project_arguments('-DNDEBUG', language: 'c')
endif

sources = [
  'src/main.c',
  'src/ui.c',
  'src/metadata.c',
  'src/utils.c',
  'src/art.c',
  'src/layout.c',
  'src/layout_compute.c',
  'src/terminal.c',
  'src/panes.c',
  'src/art_manager.c',
  'src/render_utils.c',
  'src/selector_layout.c',
  'src/config.c',
  'src/stream.c',
  'src/subprocess.c',
  'src/player_backend/player_backend.c',
  'lib/trim.c',
]

extra_deps = []
sources += ['src/player_backend/player_backend_mpris.c']

inc = include_directories('include', 'lib')

executable('yams',
  sources,
  dependencies : [ncursesw, panelw, m_dep, libavformat, libavcodec, libswscale, libavutil, chafa],
  include_directories : inc,
  install : true
)

if get_option('tests')
  cmocka_dep = dependency('cmocka', required: false)
  if not cmocka_dep.found()
    cmocka_dep = subproject('cmocka').get_variable('cmocka_dep')
  endif
  subdir('tests')
endif
