/* -*- Mode: C++; indent-tabs-mode: nil; tab-width: 4 -*-
 * -*- coding: utf-8 -*-
 *
 * Copyright (C) 2021 KylinSoft Co., Ltd.
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
#include "ukui-scale-plugin.h"

UkuiScalePlugin* UkuiScalePlugin::mScalePlugin = nullptr;

UkuiScalePlugin::UkuiScalePlugin()
{
    USD_LOG(LOG_DEBUG,"ScalePlugin initializing!");

    mScaleManager = new UkuiScaleManager();
}

UkuiScalePlugin::~UkuiScalePlugin() {
    USD_LOG(LOG_DEBUG,"ScalePlugin deconstructor!");
    if (mScaleManager) {
        delete mScaleManager;
    }
    mScaleManager = nullptr;
}


void UkuiScalePlugin::activate () {
    USD_LOG (LOG_DEBUG, "Activating %s plugin compilation time:[%s] [%s]",MODULE_NAME,__DATE__,__TIME__);

    mScaleManager->getInitialConfig();
//        USD_LOG(LOG_DEBUG,"unable to start scale manager");

}

void UkuiScalePlugin::deactivate () {
    USD_LOG(LOG_DEBUG,"Deactivating scale plugin!");
//    mScaleManager->stop();
}

PluginInterface* UkuiScalePlugin::getInstance()
{
    if(nullptr == mScalePlugin)
        mScalePlugin = new UkuiScalePlugin();
    return mScalePlugin;
}

PluginInterface* createSettingsPlugin()
{
    return UkuiScalePlugin::getInstance();
}

