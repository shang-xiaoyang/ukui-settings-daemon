

/* -*- Mode: C++; indent-tabs-mode: nil; tab-width: 4 -*-
 * -*- coding: utf-8 -*-
 *
 * Copyright (C) 2012 by Alejandro Fiestas Olivares <afiestas@kde.org>
 * Copyright 2016 by Sebastian Kügler <sebas@kde.org>
 * Copyright (c) 2018 Kai Uwe Broulik <kde@broulik.de>
 *                    Work sponsored by the LiMux project of
 *                    the city of Munich.
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
#ifndef XRANDRMANAGER_H
#define XRANDRMANAGER_H

#include <QObject>
#include <QTimer>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QGSettings/qgsettings.h>
#include <QScreen>


#include <KF5/KScreen/kscreen/config.h>
#include <KF5/KScreen/kscreen/log.h>
#include <KF5/KScreen/kscreen/output.h>
#include <KF5/KScreen/kscreen/edid.h>
#include <KF5/KScreen/kscreen/configmonitor.h>
#include <KF5/KScreen/kscreen/getconfigoperation.h>
#include <KF5/KScreen/kscreen/setconfigoperation.h>
#include <usd_base_class.h>

#include "xrandroutput.h"
#include "xrandr-dbus.h"
#include "xrandr-adaptor.h"
#include "xrandr-config.h"
#include "usd_base_class.h"
#include "usd_global_define.h"

#define SAVE_CONFIG_TIME 800

typedef struct _MapInfoFromFile
{
    QString sTouchName;     //触摸屏的名称
    QString sTouchSerial;   //触摸屏的序列号
    QString sMonitorName;   //显示器的名称
}MapInfoFromFile;             //配置文件中记录的映射关系信息
//END 触摸屏自动映射相关

typedef struct _TouchpadMap
{
    int     sTouchId;
    QString sMonitorName;   //显示器的名称
}touchpadMap;
//END 触摸屏自动映射相关

class XrandrManager: public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KScreen")

public:
    XrandrManager();
    ~XrandrManager() override;

public:
    bool start();
    void stop();
    void active();
    void initAllOutputs();
    void applyConfig();

    int getCurrentMode();
    int getCurrentRotation();
    void doOutputChanged(KScreen::Output *senderOutput);
    void doOutputModesChanged();
    void lightLastScreen();
    void outputConnectedWithoutConfigFile(KScreen::Output *senderOutput ,char outputCount);
    void setOutputsModeToClone();
    void setOutputsModeToFirst(bool isFirstMode);
    void setOutputsModeToExtend();
    bool checkPrimaryOutputsIsSetable();
    bool readAndApplyOutputsModeFromConfig(UsdBaseClass::eScreenMode eMode);
    bool checkSettable(UsdBaseClass::eScreenMode eMode);
    void sendOutputsModeToDbus();

    void calibrateTouchDevice();
    void calibrateTouchDeviceWithConfigFile(QString mapPath);
    void calibrateTouchScreen();
    void calibrateTouchScreenInTablet();
    void calibrateDevice (int input_name, char *output_name, bool isRemapFromFile = false);

    bool getOutputConnected(QString screenName);
    bool getTouchDeviceCalibrateState(int id);
    bool getOutputCalibrateState(const QString screenName);
    bool readMateToKscreen(char monitorsCount,QMap<QString, QString> &monitorsName);
    int getMateConfigParam(UsdOuputProperty *mateOutput, QString param);

    UsdBaseClass::eScreenMode discernScreenMode();
public Q_SLOTS:
    void doTabletModeChanged(const bool tablemode);
    void doOutputsConfigurationChanged();
    void doRotationChanged(const QString &rotation);
    void doOutputAdded(const KScreen::OutputPtr &output);
    void doOutputRemoved(int outputId);
    void doPrimaryOutputChanged(const KScreen::OutputPtr &output);

    void setOutputsMode(QString modeName);
    void setOutputsParam(QString screensParam);
    void doSaveConfigTimeOut();
    /*台式机screen旋转后触摸*/
    void doCalibrate(const QString screenMap);

    QString getOutputsInfo();
protected:
    QMultiMap<QString, QString> m_mateFileTag; //存放标签的属性值
    QMultiMap<QString, int>     m_IntDate;

private:
    void disableCrtc();
    Q_INVOKABLE void getInitialConfig();

private:
    enum eScreenSignal {
        isNone = 0,
        isOutputChanged = 1<<0,
        isPosChanged = 1<<1,
        isSizeChanged = 1<<2,
        isCurrentModeIdChanged = 1<<3,
        isRotationChanged = 1<<4,
        isConnectedChanged = 1<<5,
        isEnabledChanged = 1<<6,
        isPrimaryChanged = 1<<7,
        isClonesChanged = 1<<8,
        isReplicationSourceChanged = 1<<9,
        isScaleChanged = 1<<10,
        isLogicalSizeChanged = 1<<11,
        isFollowPreferredModeChanged = 1<<12,
        isModesChanged = 1 << 13,
    };

    QTimer                *m_acitveTimer = nullptr;
    QTimer                *m_outputsInitTimer = nullptr;
    QTimer                *m_screenSignalTimer = nullptr;
    QTimer                *m_offUsbScreenTimer = nullptr;
    QTimer                *m_onUsbScreenTimer = nullptr;

    QMetaEnum             m_outputModeEnum;
    QGSettings            *m_xrandrSettings = nullptr;
    QStringList           m_modesChangeOutputs;

    QDBusInterface        *m_ukccDbus = nullptr;
    QDBusInterface        *m_statusManagerDbus = nullptr;
    QList<touchpadMap*>    m_touchMapList; //存储已映射的关系

    xrandrDbus                      *m_xrandrDbus = nullptr;
    KScreen::ConfigPtr                m_configPtr = nullptr;
    std::unique_ptr<xrandrConfig> m_outputsConfig = nullptr;

    bool    m_isSetting = false;
    int     m_outputsChangedSignal = 0;
    bool    m_applyConfigWhenSave = false;

};

#endif // XRANDRMANAGER_H
