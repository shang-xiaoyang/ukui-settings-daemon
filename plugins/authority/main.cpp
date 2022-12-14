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

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>
#include "authorityservice.h"
#include "brightnesshelper.h"
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    a.setOrganizationName("Kylin Team");

    QDBusConnection systemBus = QDBusConnection::systemBus();
    if (systemBus.registerService("com.settings.daemon.qt.systemdbus")){
        systemBus.registerObject("/", new AuthorityService(),
                                 QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals
                                 );

        systemBus.registerObject("/brightness", new BrightnessHelper(),
                                 QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals
                                 );
    }

    return a.exec();
}
