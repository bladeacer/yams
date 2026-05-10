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

sources = [
  'src/main.c',
  'src/ui.c',
  'src/metadata.c',
  'src/utils.c',
  'src/art.c',
  'src/layout.c',
  'src/terminal.c',
  'src/panes.c',
  'src/art_manager.c',
  'src/render_utils.c',
  'src/config.c'
]
inc = include_directories('src')

executable('yams',
  sources,
  # 4. Add panelw to the dependencies list
  dependencies : [ncursesw, panelw, m_dep],
  include_directories : inc,
  install : true
)
