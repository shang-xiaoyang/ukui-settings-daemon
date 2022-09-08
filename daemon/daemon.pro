#-------------------------------------------------
#
# Project created by QtCreator 2020-03-16T09:30:00
#
#-------------------------------------------------
TEMPLATE = app
TARGET = ukui-settings-daemon

QT += core gui dbus
CONFIG += no_keywords link_prl link_pkgconfig c++11 debug
CONFIG -= app_bundle
DEFINES += MODULE_NAME=\\\"Daemon\\\"

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

include($$PWD/../common/common.pri)

INCLUDEPATH += \
        -I /usr/include/mate-desktop-2.0/

PKGCONFIG += \
        glib-2.0\
        gio-2.0\
        gobject-2.0\
        gmodule-2.0 \
        dconf

SOURCES += \
        $$PWD/main.cpp\
        $$PWD/plugin-info.cpp\
        $$PWD/plugin-manager.cpp\
        $$PWD/manager-interface.cpp

HEADERS += \
        $$PWD/plugin-info.h\
        $$PWD/plugin-manager.h\
        $$PWD/manager-interface.h \
        $$PWD/global.h

ukui_daemon.path = /usr/bin/
ukui_daemon.files = $$OUT_PWD/ukui-settings-daemon

qm.path  = /usr/share/ukui-settings-daemon/daemon/res/i18n/
qm.files = $$PWD/res/i18n/*.qm

zhCN.commands = $$system(msgfmt -o res/i18n/zh_CN/ukui-settings-daemon.mo res/i18n/zh_CN/ukui-settings-daemon_zh_CN.po)
boCN.commands = $$system(msgfmt -o res/i18n/bo_CN/ukui-settings-daemon.mo res/i18n/bo_CN/ukui-settings-daemon_bo_CN.po)

QMAKE_EXTRA_TARGETS += zhCN boCN
po_zh_CN.path  = /usr/share/locale/zh_CN/LC_MESSAGES/
po_zh_CN.files = res/i18n/zh_CN/ukui-settings-daemon.mo

po_bo_CN.path  = /usr/share/locale/bo_CN/LC_MESSAGES/
po_bo_CN.files = res/i18n/bo_CN/ukui-settings-daemon.mo

INSTALLS += ukui_daemon qm po_zh_CN po_bo_CN

RESOURCES += \
    $$PWD/res/zh_CN.qrc
