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
#ifndef BACKGROUND_MANAGER_H
#define BACKGROUND_MANAGER_H
#include <QObject>
#include <QGSettings/QGSettings>
#include <QDebug>
#include <QScreen>
#include <QTimer>
#include <X11/Xlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "clib-syslog.h"

#ifdef __cplusplus
}
#endif

class BackgroundManager : public QObject
{
    Q_OBJECT
public:
    BackgroundManager();
    ~BackgroundManager();

public:
    void BackgroundManagerStart();
    void SetBackground();
    void initGSettings();

    void setSolidColorBackground();
    void draw_background();

private:
    void scaleBg(const QRect &geometry);
    void virtualGeometryChangedProcess(const QRect &geometry);

public Q_SLOTS:
    void setup_Background(const QString &key);
    void screenAddedProcess(QScreen *screen);
    void screenRemovedProcess(QScreen *screen);
    void StartXrandrIdleCb();

private:
    QTimer      *mAcitveTime = nullptr;
    QGSettings  *bSettingOld;
    QScreen     *m_screen;
    QString      Filename;
    Display     *dpy = NULL;
    Pixmap       pix = NULL;
    Window       root = NULL;
    Screen      *scn = NULL;
};

#endif // BACKGROUND_MANAGER_H
