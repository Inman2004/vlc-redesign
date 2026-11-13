TEMPLATE = app

QT = core gui svg widgets
QTPLUGIN = qsvgicon qsvg qjpeg qico

# redesign: online subtitle search (Add Subtitles) when Qt Network exists
qtHaveModule(network) {
QT += network
DEFINES += HAVE_QT_NETWORK
}

CONFIG -= entrypoint
CONFIG -= debug_and_release
CONFIG += no_include_pwd

win32 {
QTPLUGIN += qwindows

versionAtLeast(QT_VERSION, 6.7.0) {
QTPLUGIN += qmodernwindowsstyle
} else {
QTPLUGIN += qwindowsvistastyle
}
}

linux {
QTPLUGIN += qxcb-glx-integration qxcb-egl-integration qxcb qwayland-generic qwayland-egl qgtk3 qxdgdesktopportal xdg-shell
}

macx {
QTPLUGIN += qcocoa qmacstyle
}
