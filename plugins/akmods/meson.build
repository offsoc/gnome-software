shared_module(
  'gs_plugin_akmods',
  sources : 'gs-plugin-akmods.c',
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : ['-DG_LOG_DOMAIN="GsPluginAkmods"'],
  dependencies : plugin_libs,
)

executable(
  'gnome-software-akmods-helper',
  sources : [
    'gs-akmods-helper.c',
  ],
  dependencies : [
    glib,
    gio_unix,
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../lib'),
  ],
  c_args : ['-DG_LOG_DOMAIN="GsAkmodsHelper"'],
  install : true,
  install_dir : get_option('libexecdir'),
)
