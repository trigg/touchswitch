all_deps = [wayfire, rsvg, cairo]

shared_module(
        'touchswitch',
        [
                'touchswitch.cpp',
                'touchswitch-title-overlay.cpp',
                'touchswitch-icon-overlay.cpp',
        ],
        dependencies: all_deps,
        install: true,
        install_dir: join_paths(get_option('libdir'), 'wayfire'),
)