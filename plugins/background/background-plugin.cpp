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
#include <QApplication>
#include <QScreen>
#include "background-plugin.h"
#include "clib-syslog.h"

PluginInterface* BackgroundPlugin::mInstance = nullptr;

BackgroundPlugin::BackgroundPlugin()
{
    manager = new BackgroundManager();
    return;
}

BackgroundPlugin::~BackgroundPlugin()
{
    USD_LOG(LOG_DEBUG, "background plugin free...");
    if(manager){
        delete manager;
        manager = nullptr;
    }
}

PluginInterface *BackgroundPlugin::getInstance()
{
    if (nullptr == mInstance) {
        mInstance = new BackgroundPlugin();
    }

    return mInstance;
}

void BackgroundPlugin::activate()
{
    USD_LOG (LOG_DEBUG, "Activating %s plugin compilation time:[%s] [%s]",MODULE_NAME,__DATE__,__TIME__);
     manager->BackgroundManagerStart();
}

void BackgroundPlugin::deactivate()
{
    USD_LOG (LOG_DEBUG, "Deactivating background plugin");
}

PluginInterface* createSettingsPlugin()
{
    return BackgroundPlugin::getInstance();
}
