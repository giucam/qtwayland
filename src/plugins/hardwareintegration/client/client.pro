include(../../../../modules.pri)

TEMPLATE=subdirs

build_wayland_egl: \
    SUBDIRS += wayland-egl

build_brcm_egl: \
    SUBDIRS += brcm-egl

build_xcomposite_egl: \
    SUBDIRS += xcomposite-egl

build_xcomposite_glx: \
    SUBDIRS += xcomposite-glx

build_drm_egl_server: \
    SUBDIRS += drm-egl-server

build_libhybris_egl_server: \
    SUBDIRS += libhybris-egl-server
