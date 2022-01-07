project('difftree', 'c',
  version : '0.1')

add_project_arguments('-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56', language : 'c')
add_project_arguments('-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_56', language : 'c')

dep_gtk = dependency('gtk+-3.0', required: true)
dep_gio = dependency('gio-2.0', required: true)
dep_zip = dependency('libzip', required: true)

executable('difftree',
  'app-config.c',
  'child-process-util.c',
  'diff-tree-main.c',
  'diff-tree-model.c',
  'diff-tree-view.c',
  'ref-count-struct.c',
  'settings-window.c',
  'source-helpers.c',
  'tree-source-base.c',
  'tree-source-fs.c',
  'tree-source-zip.c',
  'tree-source.c',
  'zip-input-stream.c',
  'zipfd.c',
  dependencies : [ dep_gtk, dep_gio, dep_zip ],
  install : true
)
