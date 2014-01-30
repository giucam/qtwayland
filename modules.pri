
contains(BUILD_MODULES, wayland_compositor): \
    CONFIG += build_wayland_compositor

contains(BUILD_MODULES, wayland_client): \
    CONFIG += build_wayland_client

contains(BUILD_MODULES, qtwaylandscanner): \
    CONFIG += build_qtwaylandscanner

contains(BUILD_MODULES, wayland_generic_plugin): \
    CONFIG += build_wayland_generic

config_wayland_egl: contains(BUILD_MODULES, wayland_egl_plugin): \
    CONFIG += build_wayland_egl


config_brcm_egl: contains(BUILD_MODULES, brcm_egl_plugin): \
    CONFIG += build_brcm_egl

config_xcomposite {
    config_egl: contains(BUILD_MODULES, xcomposite-egl_plugin): \
        CONFIG += build_xcomposite_egl

    !contains(QT_CONFIG, opengles2): config_glx: contains(BUILD_MODULES, xcomposite-glx_plugin): \
        CONFIG += build_xcomposite_glx
}

config_drm_egl_server: contains(BUILD_MODULES, drm_egl_server_plugin): \
    CONFIG += build_drm_egl_server

config_libhybris_egl_server: contains(BUILD_MODULES, libhybris_egl_server_plugin): \
    CONFIG += build_libhybris_egl_server