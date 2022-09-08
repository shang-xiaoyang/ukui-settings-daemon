QT += gui widgets core dbus xml

TARGET = scale
TEMPLATE = lib

DEFINES += SHARING_LIBRARY

CONFIG += c++11 no_keywords link_pkgconfig plugin
CONFIG += app_bundle

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS MODULE_NAME=\\\"scale\\\"

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include($$PWD/../../common/common.pri)

PKGCONFIG += glib-2.0   gio-2.0 xcursor xfixes \
             xcb-randr  x11-xcb x11     xrandr \
             graphene-gobject-1.0 gsettings-qt

SOURCES += \
        $$PWD/ukui-scale-plugin.cpp \
        $$PWD/ukui-scale-manager.cpp \
        $$PWD/ukui-gpu-xrandr.cpp \
        $$PWD/ukui-scale-dbus.cpp \
        $$PWD/ukui-scale-adaptor.cpp

HEADERS += \
    $$PWD/ukui-scale-manager.h \
    $$PWD/ukui-scale-plugin.h \
    $$PWD/ukui-gpu-xrandr.h \
    $$PWD/ukui-scale-dbus.h \
    $$PWD/ukui-scale-adaptor.h

ukui_scale.path = $${PLUGIN_INSTALL_DIRS}
ukui_scale.files = $$OUT_PWD/libscale.so

INSTALLS += ukui_scale




