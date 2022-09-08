/* -*- Mode: C++; indent-tabs-mode: nil; tab-width: 4 -*-
 * -*- coding: utf-8 -*-
 *
 * Copyright (C) 2020 KylinSoft Co., Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xrandr-dbus.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QProcess>
#include <QDebug>
#include <QAction>
#include <KF5/KGlobalAccel/KGlobalAccel>

#include <glib.h>
#include <gio/gio.h>

#include "clib-syslog.h"
#include "usd_base_class.h"

#include "xrandr-manager.h"

#define UKUI_DAEMON_NAME    "ukui-settings-daemon"
XrandrManager *xrandrManager = nullptr;
xrandrDbus::xrandrDbus(QObject* parent) : QObject(parent)
{
    mXsettings =new QGSettings("org.ukui.SettingsDaemon.plugins.xsettings");
    mScale = mXsettings->get("scaling-factor").toDouble();
    xrandrManager = static_cast<XrandrManager*>(parent);

    //QDBusConnection::sessionBus().unregisterService("org.ukui.SettingsDaemon.xrandr");
    //QDBusConnection::sessionBus().registerService("org.ukui.SettingsDaemon.xrandr");
    QDBusConnection::sessionBus().registerObject("0",this,QDBusConnection::ExportAllSlots);
}

xrandrDbus::~xrandrDbus()
{
    delete mXsettings;
}

int xrandrDbus::setScreenMode(QString modeName,QString appName){
    USD_LOG(LOG_DEBUG,"change screen :%s, appName:%s",modeName.toLatin1().data(), appName.toLatin1().data());
    Q_EMIT setScreenModeSignal(modeName);
    return true;
}

int xrandrDbus::getScreenMode(QString appName){
    USD_LOG(LOG_DEBUG,"get screen mode appName:%s", appName.toLatin1().data());
    return xrandrManager->discernScreenMode();
}

int xrandrDbus::setScreensParam(QString screensParam, QString appName)
{
    USD_LOG(LOG_DEBUG,"appName:%s",screensParam.toLatin1().data(),appName);
    Q_EMIT setScreensParamSignal(screensParam);
    return 1;
}

QString xrandrDbus::getScreensParam(QString appName)
{
    USD_LOG(LOG_DEBUG,"dbus from %s",appName.toLatin1().data());
    return xrandrManager->getOutputsInfo();
}

void xrandrDbus::sendModeChangeSignal(int screensMode)
{
    static int lastScreenMode = 0xff;
    if (lastScreenMode == screensMode) {
        return;
    }

    lastScreenMode = screensMode;
    USD_LOG(LOG_DEBUG,"send mode:%d",screensMode);

    Q_EMIT screenModeChanged(screensMode);
}

void xrandrDbus::sendScreensParamChangeSignal(QString screensParam)
{
//    USD_LOG(LOG_DEBUG,"send param:%s",screensParam.toLatin1().data());
     Q_EMIT screensParamChanged(screensParam);
}

void xrandrDbus::setScreenMap()
{
    xrandrManager->calibrateTouchDevice();
}

QString xrandrDbus::controlScreenSlot(const QString &conRotation)
{
    USD_LOG(LOG_DEBUG,"control call this slot");
    Q_EMIT controlScreen(conRotation);
    return QString("Get messageMethod reply: %1").arg(conRotation);
}


