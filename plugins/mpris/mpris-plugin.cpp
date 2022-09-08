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
#include "mpris-plugin.h"
#include "clib-syslog.h"

PluginInterface* MprisPlugin::mInstance = nullptr;

MprisPlugin::MprisPlugin()
{
    USD_LOG(LOG_DEBUG,"UsdMprisPlugin initializing!");
    mprisManager = MprisManager::MprisManagerNew();
}

MprisPlugin::~MprisPlugin()
{
    USD_LOG(LOG_DEBUG,"UsdMprisPlugin deconstructor!");
    if(mprisManager)
        delete mprisManager;
}

void MprisPlugin::activate()
{
        gboolean res;
        GError  *error;
        USD_LOG (LOG_DEBUG, "Activating %s plugin compilation time:[%s] [%s]",MODULE_NAME,__DATE__,__TIME__);


        error = NULL;
        res = mprisManager->MprisManagerStart(&error);
        if (! res) {
                USD_LOG(LOG_WARNING,"Unable to start mpris manager: %s", error->message);
                g_error_free (error);
        }
}

void MprisPlugin::deactivate()
{
        USD_LOG(LOG_DEBUG,"Deactivating mpris plugin");
        mprisManager->MprisManagerStop();
}

PluginInterface* MprisPlugin::getInstance()
{
    if(nullptr == mInstance)
        mInstance = new MprisPlugin();
    return mInstance;
}

PluginInterface* createSettingsPlugin()
{
    return  MprisPlugin::getInstance();
}
