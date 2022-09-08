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
#ifndef __BRIGHTNESSHELPER_H__
#define __BRIGHTNESSHELPER_H__
#include <QObject>
#include <QVector>
#include <ddcutil_c_api.h>
#include <ddcutil_types.h>
#include "xcbeventlistener.h"
/*
 *TODO:
 * ddcd、i2c的通用性没有XRR接口广，目前方案是否评估到
 *
*/
struct outputInfo {
    bool   _DDC;           //是否采用DDC处理，当DDC失败时使用I2C
    DDCA_Display_Handle ddca_dh_loc;   //显示器句柄
    DDCA_Display_Ref ddca_ref;
    DDCA_Display_Identifier did;
    QString edidHash;      //edid信息的hash值(md5)
    QString serialId;      //显示器序列号
    QString outputName;      //显示器序列号
    QString I2C_busType;       //兼容I2C处理亮度
    int     I2C_brightness;
    int     isPrimary;
    int     rtBrightness;       //实时亮度
    int     setable;        //可以被设置
    bool operator == (const QString &edidHash) {
        return this->edidHash.contains(edidHash);
    }
};

class BrightnessHelper:public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","com.settings.daemon.interface")
public:
    BrightnessHelper(QObject *parent = nullptr);
    ~BrightnessHelper();
public Q_SLOTS:
    //台式机亮度设计方案：
    Q_SCRIPTABLE void reinitOutputInfo();
    Q_SCRIPTABLE int getOutputBrightnessWithHash(QString edidHash);
    Q_SCRIPTABLE int setDisplayBrightnessWithHash(uint brightness, QString edidHash);
    //不太适合用serial，会出现同serial的情况。
    Q_SCRIPTABLE int getDisplayBrightnessWithSerial(QString serialId);
    Q_SCRIPTABLE int setDisplayBrightnessWithSerial(uint brightness, QString serialId);
    Q_SCRIPTABLE int setOutputPrimary(QString edidHash, QString outputName, bool isPrimary);
    Q_SCRIPTABLE QString checkBrightnessSetable();
    Q_SCRIPTABLE QString checkBrightnessAllstate();

private:

    void getDisplayInfo();
    void initOutputInfoThread();
    int setI2CBrightness(uint brightness, QString type);
    int getI2CBrightness(QString type);
    QString getOutputSetableInfo();
private:
    QVector<struct outputInfo> m_displaysInfo;
    bool m_isGetDisplayInfo;
    volatile bool m_exitGetTread;
    DDCA_Display_Handle m_ddcaDH = nullptr;

Q_SIGNALS:
    void outputBrightnessChanged(QString outputName, int brightness);
};

#endif // __BRIGHTNESSHELPER_H__
