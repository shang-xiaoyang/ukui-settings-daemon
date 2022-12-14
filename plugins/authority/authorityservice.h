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

#ifndef AUTHORITYSERVICE_H
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
#define AUTHORITYSERVICE_H

#include <QObject>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include "brightnesshelper.h"
class AuthorityService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","com.settings.daemon.interface")

public:
    explicit AuthorityService(QObject *parent = nullptr);
    ~AuthorityService(){}
public Q_SLOTS:
    Q_SCRIPTABLE QString getCameraBusinfo();
    Q_SCRIPTABLE QString toggleCameraDevice();

    Q_SCRIPTABLE int getCameraDeviceEnable();
    Q_SCRIPTABLE int setCameraKeyboardLight(bool lightup);
    Q_SCRIPTABLE int isTrialMode();
    Q_SCRIPTABLE int setDynamicBrightness(bool state);
    Q_SCRIPTABLE int nightModeCanBeSet();
private:
    BrightnessHelper *brightnessHelper;
    QString popenCommand(QString cmd);
    QStringList m_NightModeBlackList = {"Device 0709:0001  (rev 01)"};
    QStringList m_BrightnessBlackList = {"",""};

Q_SIGNALS:
    void brightnessChanged(QString outputName, bool isPrimary, int brightness);
};

#endif // AUTHORITYSERVICE_H
