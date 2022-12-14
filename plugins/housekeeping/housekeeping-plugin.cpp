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
#include <QString>
#include <QFile>
#include <QProcess>
#include <QDBusInterface>
#include <QDBusReply>
#include "usd_base_class.h"
#include "housekeeping-plugin.h"
#include "clib-syslog.h"

PluginInterface     *HousekeepingPlugin::mInstance=nullptr;

QString getCurrentUserName()
{
    QString name;
    if (name.isEmpty()) { 
        QStringList envList = QProcess::systemEnvironment();
        for(const QString& env : envList){
            if (env.startsWith("USERNAME")) {
                QStringList strList = env.split('=');
                if (strList.size() > 2) {
                    name = strList[1];
                }
            }
        }
    }
    if (!name.isEmpty())
        return name;
    QProcess process;
    process.start("whoami", QStringList());
    process.waitForFinished();
    name = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    return name.isEmpty() ? QString("User") : name;
}

HousekeepingPlugin::HousekeepingPlugin()
{
    if (isInTrialMode()) {
        USD_LOG(LOG_DEBUG,"TrialMode...");
        return;
    }
    userName = getCurrentUserName();
    if (userName.compare("lightdm") != 0) {
        mHouseManager = new HousekeepingManager();
        if (!mHouseManager)
            USD_LOG(LOG_ERR,"Unable to start Housekeeping Manager!");
    }
}

HousekeepingPlugin::~HousekeepingPlugin()
{
    if (mHouseManager) {
        delete mHouseManager;
        mHouseManager = nullptr;
    }
}

bool HousekeepingPlugin::isInTrialMode()
{
    QString str = "";
    QStringList symbList ;
    QFile file("/proc/cmdline");
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        str = QString::fromLocal8Bit(data);
        symbList = str.split("\r\n");
    }

    USD_LOG(LOG_DEBUG,"cmdline:%s",str.toLatin1().data());
    file.close();

    if (str.contains("boot=casper")) {
        return true;
    }

    if (getuid() == 999)
        return true;

    return false;
}



void HousekeepingPlugin::activate()
{
    if (isInTrialMode()) {//TrialMode disable this plugin
        USD_LOG(LOG_DEBUG,"TrialMode...");
        return;
    }

    if (userName.compare("lightdm") != 0) {
        USD_LOG(LOG_DEBUG,"Housekeeping Manager Is Start");
        mHouseManager->HousekeepingManagerStart();
    }
}

PluginInterface *HousekeepingPlugin::getInstance()
{
    if (nullptr == mInstance) {
        mInstance = new HousekeepingPlugin();
    }
    return mInstance;
}

void HousekeepingPlugin::deactivate()
{
    if(isInTrialMode()) {
        return;
    }

    if (mHouseManager) {
        mHouseManager->HousekeepingManagerStop();
    }
}

PluginInterface *createSettingsPlugin()
{
    return HousekeepingPlugin::getInstance();
}
