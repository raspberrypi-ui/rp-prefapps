sources = files (
    'rp_prefapps.c'
)

add_global_arguments('-Wno-unused-result', language : 'c')

gtk = dependency ('gtk+-3.0')
packagekit = dependency('packagekit-glib2')
deps = [ gtk, packagekit ]

executable (meson.project_name(), sources, dependencies: deps, install: true)
