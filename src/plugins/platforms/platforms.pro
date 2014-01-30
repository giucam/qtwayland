include(../../../modules.pri)

TEMPLATE=subdirs
CONFIG+=ordered

build_wayland_generic: \
    SUBDIRS += qwayland-generic

build_wayland_egl: \
    SUBDIRS += qwayland-egl

#The following integrations are only useful with QtCompositor
build_wayland_compositor) {
    build_brcm_egl: \
        SUBDIRS += qwayland-brcm-egl

    build_xcomposite_egl: \
        SUBDIRS += qwayland-xcomposite-egl
    build_xcomposite_glx: \
        SUBDIRS += qwayland-xcomposite-glx
}
