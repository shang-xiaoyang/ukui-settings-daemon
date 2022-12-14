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
#include <QDebug>
#include "ukui-xsettings-plugin.h"
#include "clib-syslog.h"

PluginInterface *XSettingsPlugin::mInstance =nullptr;
ukuiXSettingsManager *XSettingsPlugin::m_pukuiXsettingManager = nullptr;

XSettingsPlugin::XSettingsPlugin()
{
    if(nullptr == m_pukuiXsettingManager)
        m_pukuiXsettingManager = new ukuiXSettingsManager();
}

XSettingsPlugin::~XSettingsPlugin()
{
    if (m_pukuiXsettingManager) {
        delete m_pukuiXsettingManager;
        m_pukuiXsettingManager = nullptr;
    }
}

void XSettingsPlugin::activate()
{
    bool res;

    res = m_pukuiXsettingManager->start();
    if (!res) {
        qWarning ("Unable to start XSettingsPlugin manager");
    }
    USD_LOG (LOG_DEBUG, "Activating %s plugin compilation time:[%s] [%s]",MODULE_NAME,__DATE__,__TIME__);

}

void XSettingsPlugin::deactivate()
{
    m_pukuiXsettingManager->stop();
}
PluginInterface *XSettingsPlugin::getInstance()
{
    if(nullptr == mInstance)
        mInstance = new XSettingsPlugin();
    return mInstance;
}
PluginInterface* createSettingsPlugin() {
    return XSettingsPlugin::getInstance();
}
