project('yams', 'c',
  version : '1.0.0',
  default_options : [
    'c_std=c11',
    'warning_level=3',
    'buildtype=debugoptimized',
    'optimization=s'
  ]
)

ncurses = dependency('ncurses')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

sources = ['yams.c']

executable('yams',
  sources,
  dependencies : [ncurses, m_dep],
  install : true
)
