include(../modules.pri)

TEMPLATE=subdirs
CONFIG+=ordered

build_qtwaylandscanner: \
    SUBDIRS += qtwaylandscanner

#Don't build QtCompositor API unless explicitly enabled
build_wayland_compositor {
    SUBDIRS += compositor
}

build_wayland_client: \
    SUBDIRS += client

SUBDIRS += plugins
