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


#ifndef KDSWIDGET_H
#define KDSWIDGET_H

#include <QObject>
#include <KF5/KScreen/kscreen/output.h>
#include <KF5/KScreen/kscreen/edid.h>
#include <KF5/KScreen/kscreen/mode.h>
#include <KF5/KScreen/kscreen/config.h>
#include <KF5/KScreen/kscreen/getconfigoperation.h>
#include <KF5/KScreen/kscreen/setconfigoperation.h>

#include "xrandr-config.h"
#include "xrandroutput.h"

extern "C"{
//#include <glib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>
}

typedef struct tag_X11OutputInfo{
    XID outputId;
    XRROutputInfo *outputInfo;
    XRRModeInfo	*modes;
}X11OutputInfo;

class SaveScreenParam :  public QObject
{
    Q_OBJECT

//    Q_PROPERTY(bool isSet READ isSet WRITE setIsSet )
//    Q_PROPERTY(bool isGet READ isGet WRITE setIsGet )

public:

    explicit SaveScreenParam(QObject *parent = nullptr) ;
    ~SaveScreenParam();
    void setIsSet(bool value)
    {
        m_isSet = value;
    }

    void setIsGet(bool value)
    {
        m_isGet = value;
    }

    bool isSet() const
    {
        return m_isSet;
    }

    bool isGet() const
    {
        return m_isGet;
    }

    void getConfig();

    void setWithoutConfig(OutputsConfig *outputsConfig);

    void setUserConfigParam();

    void setUserName(QString str);

    void setScreenSize();

    void readKscreenConfigAndSetItWithX(QString kscreenConfigName = "");


    void disableCrtc();
    //kscreen ??????????????????
    void readConfigAndSetBak();

    QString printUserConfigParam();

    void setXdpi(double scale);
private:
    bool m_isSet;
    bool m_isGet;
    int	m_screen;
    QString m_KscreenConfigFile;
    char   *m_pDisplayName = NULL;

    Display	*m_pDpy;
    Window	m_rootWindow;
    XRRScreenResources  *m_pScreenRes;

    QString m_userName;
    std::unique_ptr<xrandrConfig> m_MonitoredConfig = nullptr;

private:
    OutputsConfig m_kscreenConfigParam;
    int initXparam();

    bool checkTransformat(RRCrtc crtc, double scale);
    bool clearCrtc(RRCrtc crtc);
    bool readKscreenConfig(OutputsConfig *outputsConfig, QString configFullPath);

    void getRootWindows();
    void getScreen();
    void debugAllOutput(OutputsConfig *outputsConfig);
    void clearAndGetCrtcs(OutputsConfig *outputsConfig);
    void setCrtcWith(OutputsConfig *outputsConfig);
    void setCrtcConfig(OutputsConfig *outputsConfig);
    QString showAllOutputInJson(OutputsConfig *outputsConfig);
    double getScaleWithSize(int width, int height, int widthmm, int heightmm);
    double getScale(double scaling);
    double getScoreScale(double scaling);


    RRMode getModeId(XRROutputInfo	*outputInfo, UsdOuputProperty *kscreenOutputParam);
    QString getKscreenConfigFullPathInLightDM();

};

#endif // KDSWIDGET_H
