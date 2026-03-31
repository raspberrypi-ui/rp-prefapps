sources = files (
    'rp_prefapps.c',
    'activate.c'
)

gtk = dependency ('gtk+-3.0')
packagekit = dependency('packagekit-glib2')
deps = [ deps, gtk, packagekit ]

executable (meson.project_name(), sources, dependencies: deps, install: true)
