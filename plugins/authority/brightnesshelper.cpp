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

#include <unistd.h>
#include <QProcess>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QtConcurrent/QtConcurrent>
#include "brightnesshelper.h"
#define QDEBUG(a) qDebug()<<#a<<a<<"at"<<__LINE__;



BrightnessHelper::BrightnessHelper(QObject *parent)
    :QObject(parent),
      m_exitGetTread(false),
      m_isGetDisplayInfo(true)
{
//    xcbListener = new XCBEventListener();//will panic....
    reinitOutputInfo();
}

BrightnessHelper::~BrightnessHelper()
{
    m_exitGetTread = true;
}

void BrightnessHelper::reinitOutputInfo()
{
    bool include_invalid_displays = true;
    DDCA_Display_Info_List*  dlist_loc = nullptr;
    ddca_get_display_info_list2(include_invalid_displays, &dlist_loc);

    for (int i = 0; i < dlist_loc->ct; i++) {
        QCryptographicHash Hash(QCryptographicHash::Md5);
        Hash.reset();
        Hash.addData(reinterpret_cast<const char *>(dlist_loc->info[i].edid_bytes), 128);
        QByteArray md5 = Hash.result().toHex();
        QString edidHash = QString(md5);
        QVector<outputInfo>::iterator iter;
        iter = qFind(m_displaysInfo.begin(), m_displaysInfo.end(), edidHash);
        if (iter != m_displaysInfo.end()) {
            if (false == iter->_DDC) {
                for (int i = 0; i < 3; i++) {
                    iter->I2C_brightness = getI2CBrightness(iter->I2C_busType); //重新获取亮度
                    QDEBUG(iter->I2C_brightness);
                    if (iter->I2C_brightness > 0) {
                        break;
                    }
                }
            } else { //有的显示器刚开始是valid
//                m_displaysInfo.erase(iter);
                QDEBUG("erase iter....");
            }
        } else {

            struct outputInfo output;
            output.I2C_brightness = 0;
            output.edidHash = edidHash;
            output.serialId = QString(dlist_loc->info[i].sn);
            if (dlist_loc->info[i].dispno<0) {
                output._DDC = false;
                for (int i = 0; i < 3; i++) {
                    output.I2C_brightness = getI2CBrightness(iter->I2C_busType); //重新获取亮度
                    QDEBUG(output.I2C_brightness);
                    if (output.I2C_brightness > 0) {
                        break;
                    }
                    sleep(1);
                }
            } else {
                output._DDC = true;
                DDCA_Display_Identifier did;
                DDCA_Display_Ref ddca_dref;
                ddca_create_edid_display_identifier(dlist_loc->info[i].edid_bytes, &did);
                ddca_create_display_ref(did,&ddca_dref);
                ddca_open_display2(ddca_dref,false, &output.ddca_dh_loc);
            }
            output.I2C_busType = QString::number(dlist_loc->info[i].path.path.i2c_busno);
            m_displaysInfo.append(output);
        }
    }

    ddca_free_display_info_list(dlist_loc);
    m_isGetDisplayInfo = false;
}

int BrightnessHelper::getOutputBrightnessWithHash(QString edidHash)
{
    bool edidExist = false;
    for (int j = 0; j < m_displaysInfo.size(); j++) {
        if (m_displaysInfo[j].edidHash == edidHash) {
            edidExist = true;
            if (true == m_displaysInfo[j]._DDC) {
                DDCA_Non_Table_Vcp_Value  valrec;
                if (ddca_get_non_table_vcp_value(m_displaysInfo[j].ddca_dh_loc,0x10,&valrec) == 0) {
    //                uint16_t max_val = valrec.mh << 8 | valrec.ml; 暂未使用
                    int cur_val = valrec.sh << 8 | valrec.sl;
                     QDEBUG(cur_val);
                    return cur_val;
                } else {
                    getDisplayInfo();
                    return -2;
                }
            } else {
                if (m_displaysInfo[j].I2C_brightness >=0 && m_displaysInfo[j].I2C_brightness <= 100) {
                    QDEBUG(m_displaysInfo[j].I2C_brightness);
                    return m_displaysInfo[j].I2C_brightness;
                } else {
                    getDisplayInfo();
                    return -2;
                }
            }
        }
    }
    if (!edidExist) {
        getDisplayInfo();
    }
    return -2;
}

int BrightnessHelper::setDisplayBrightnessWithHash(uint brightness, QString edidHash)
{
    bool edidExist = false;
    if (brightness>100) {
        return 0;
    }
    for (int j = 0; j < m_displaysInfo.size(); j++) {
        if (m_displaysInfo[j].edidHash == edidHash) {
            edidExist = true;
            if (true == m_displaysInfo[j]._DDC) {
                uint8_t new_sh = brightness >> 8;
                uint8_t new_sl = brightness & 0xff;
                ddca_set_non_table_vcp_value(m_displaysInfo[j].ddca_dh_loc,0x10,new_sh,new_sl);
            } else {
                setI2CBrightness(brightness, m_displaysInfo[j].I2C_busType);
                m_displaysInfo[j].I2C_brightness = brightness;
            }
        }
    }
    if (!edidExist) {
        getDisplayInfo();
    }
    return 0;
}

int BrightnessHelper::getDisplayBrightnessWithSerial(QString serialId)
{
    bool edidExist = false;
    for (int j = 0; j < m_displaysInfo.size(); j++) {
        if (m_displaysInfo[j].serialId == serialId) {
            edidExist = true;
            if (true == m_displaysInfo[j]._DDC) {
                DDCA_Non_Table_Vcp_Value  valrec;
                if (ddca_get_non_table_vcp_value(m_displaysInfo[j].ddca_dh_loc,0x10,&valrec) == 0) {
    //                uint16_t max_val = valrec.mh << 8 | valrec.ml; 暂未使用
                    uint16_t cur_val = valrec.sh << 8 | valrec.sl;
                    return cur_val;
                } else {
                    getDisplayInfo();
                    return -2;
                }
            } else {
                if (m_displaysInfo[j].I2C_brightness >=0 && m_displaysInfo[j].I2C_brightness <= 100) {
                    return m_displaysInfo[j].I2C_brightness;
                } else {
                    getDisplayInfo();
                    return -2;
                }
            }
        }
    }
    if (!edidExist) {
        getDisplayInfo();
    }
    return -2;
}

int BrightnessHelper::setDisplayBrightnessWithSerial(uint brightness, QString serialId)
{
    bool edidExist = false;
    if (brightness>100) {
        return -1;
    }

    for (int j = 0; j < m_displaysInfo.size(); j++) {
        if (m_displaysInfo[j].serialId == serialId) {
            edidExist = true;
            if (true == m_displaysInfo[j]._DDC) {
                uint8_t new_sh = brightness >> 8;
                uint8_t new_sl = brightness & 0xff;
                ddca_set_non_table_vcp_value(m_displaysInfo[j].ddca_dh_loc,0x10,new_sh,new_sl);
            } else {
                setI2CBrightness(brightness, m_displaysInfo[j].I2C_busType);
                m_displaysInfo[j].I2C_brightness = brightness;
            }
        }
    }
    if (!edidExist) {
        getDisplayInfo();
    }
    return 0;
}

int BrightnessHelper::setOutputPrimary(QString edidHash, QString outputName, bool isPrimary)
{
    for (int j = 0; j < m_displaysInfo.size(); j++) {
        if (m_displaysInfo[j].edidHash == edidHash) {
            m_displaysInfo[j].outputName = outputName;
            m_displaysInfo[j].isPrimary = isPrimary;
            break;
        }
    }
    return 0;
}

QString BrightnessHelper::checkBrightnessSetable()
{
    if (m_isGetDisplayInfo) {
        QDEBUG("BUSY...");
        return "";
    }
    m_isGetDisplayInfo = true;
    for (int j = 0; j < m_displaysInfo.size(); j++) {
        int initValue = getOutputBrightnessWithHash(m_displaysInfo[j].edidHash);
        int checkValue = 0;
        if (!initValue) {
            checkValue = 1;
        } else {
            checkValue = initValue-1;
        }
        setDisplayBrightnessWithHash(checkValue, m_displaysInfo[j].edidHash);
        if (getOutputBrightnessWithHash(m_displaysInfo[j].edidHash) == initValue) {
            m_displaysInfo[j].setable = false;
        } else {
            m_displaysInfo[j].setable = true;
            m_displaysInfo[j].rtBrightness = initValue;
            setDisplayBrightnessWithHash(initValue, m_displaysInfo[j].edidHash);
        }
    }

    m_isGetDisplayInfo = false;
    QDEBUG("BUSY...1");
    return getOutputSetableInfo();
}

QString BrightnessHelper::checkBrightnessAllstate()
{
    QString ret = "";
    int bright;
    for (int j = 0; j < m_displaysInfo.size(); j++) {
        bright = getOutputBrightnessWithHash(m_displaysInfo[j].edidHash);
        ret += QString("!:{%1}->{%2}\n").arg(m_displaysInfo[j].edidHash).arg(bright);
    }
    return ret;
}

void BrightnessHelper::initOutputInfoThread()
{
    QtConcurrent::run([=] {  //运行独立线程去获取ddc信息，不能每次重新运行run，会导致获取的信息不对
        while (true) {
            if (m_exitGetTread)
                return;
            if (!m_isGetDisplayInfo) {
                sleep(1);
                continue;
            }
            bool include_invalid_displays = true;
            DDCA_Display_Info_List*  dlist_loc = nullptr;
            ddca_get_display_info_list2(include_invalid_displays, &dlist_loc);
            for(int i = 0; i < dlist_loc->ct; i++) {
                QCryptographicHash Hash(QCryptographicHash::Md5);
                Hash.reset();
                Hash.addData(reinterpret_cast<const char *>(dlist_loc->info[i].edid_bytes), 128);
                QByteArray md5 = Hash.result().toHex();
                QString edidHash = QString(md5);
                if (dlist_loc->info[i].dispno < 0) {  //this display is invalid for DDC.
                    bool edidExist = false;
                    for (int j = 0; j < m_displaysInfo.size(); j++) {
                        if (edidHash != m_displaysInfo[j].edidHash) {
                            continue;
                        }

                        if (false == m_displaysInfo[j]._DDC) {
                            edidExist = true;
                            for (int i = 0; i < 3; i++) {
                                m_displaysInfo[j].I2C_brightness = getI2CBrightness(m_displaysInfo[j].I2C_busType); //重新获取亮度
                                if (m_displaysInfo[j].I2C_brightness > 0) {
                                    break;
                                }
                                sleep(1);
                            }
                        } else { //有的显示器刚开始是valid
                            m_displaysInfo.remove(j);
                            edidExist = false;
                        }
                        break;
                    }
                    if (false == edidExist) {
                        struct outputInfo display;
                        display.edidHash = edidHash;
                        display.serialId = QString(dlist_loc->info[i].sn);
                        display._DDC = false;
                        display.I2C_busType = QString::number(dlist_loc->info[i].path.path.i2c_busno);
                        for (int i = 0; i < 3; i++) {
                            display.I2C_brightness = getI2CBrightness(display.I2C_busType);
                            if (display.I2C_brightness > 0) {
                                break;
                            }
                            sleep(1);
                        }
                        m_displaysInfo.append(display);
                    }
                } else {  //this display is valid for DDC.
                    bool edidExist = false;
                    for (int j = 0; j < m_displaysInfo.size(); j++) {
                        if (edidHash == m_displaysInfo[j].edidHash) {
                            if (true == m_displaysInfo[j]._DDC) {
                                edidExist = true;
                            } else { //有的显示器刚开始是invalid
                                m_displaysInfo.remove(j);
                                edidExist = false;
                            }
                            break;
                        }
                    }
                    if (!edidExist) {
                        struct outputInfo display;
                        DDCA_Display_Identifier did;
                        DDCA_Display_Ref ddca_dref;
                        display._DDC = true;
                        display.edidHash = edidHash;
                        display.I2C_busType = QString::number(dlist_loc->info[i].path.path.i2c_busno);
                        ddca_create_edid_display_identifier(dlist_loc->info[i].edid_bytes,&did);
                        ddca_create_display_ref(did,&ddca_dref);
                        ddca_open_display2(ddca_dref,false,&display.ddca_dh_loc);
                        m_displaysInfo.append(display);
                    }
                }
            }
            ddca_free_display_info_list(dlist_loc);
            m_isGetDisplayInfo = false;
        }
    });
}

void BrightnessHelper::getDisplayInfo()
{
    m_isGetDisplayInfo = true;
    return;
}

int BrightnessHelper::setI2CBrightness(uint brightness, QString type)
{
    QString program = "/usr/sbin/i2ctransfer";
    QStringList arg;
    int br = brightness;
    QString light = "0x" + QString::number(br,16);
    QString c = "0x" + QString::number(168^br,16);
    arg << "-f" << "-y" << type << "w7@0x37" << "0x51" << "0x84" << "0x03"
        << "0x10" << "0x00" << light << c;
    QProcess *vcpPro = new QProcess(this);
//    vcpPro->start(program, arg);
//    vcpPro->waitForStarted();
//    vcpPro->waitForFinished();
    vcpPro->startDetached(program, arg);
}

int BrightnessHelper::getI2CBrightness(QString type)
{
    QString program = "/usr/sbin/i2ctransfer";
    QStringList arg;
    arg<<"-f"<<"-y"<<type<<"w5@0x37"<<"0x51"<<"0x82"<<"0x01"<<"0x10"<<"0xac";
    QProcess *vcpPro = new QProcess();
    vcpPro->start(program, arg);
    vcpPro->waitForStarted();
    vcpPro->waitForFinished();
    arg.clear();
    arg<<"-f"<<"-y"<<type<<"r16@0x37";
    usleep(40000);
    vcpPro->start(program, arg);
    vcpPro->waitForStarted();
    vcpPro->waitForFinished();
    QString result = vcpPro->readAllStandardOutput().trimmed();
    if (result == "")
        return -1;

    QString bri=result.split(" ").at(9);
    bool ok;
    int bright=bri.toInt(&ok,16);
    if(ok && bright >= 0 && bright <= 100)  // == 0 maybe means failed
        return bright;

    return -1;
}

QString BrightnessHelper::getOutputSetableInfo()
{
    QJsonArray arrary;
    QJsonDocument jdoc;
    QDEBUG(m_displaysInfo.size());
    for (int j = 0; j < m_displaysInfo.size(); j++) {
        QJsonObject sub;
        sub.insert("hash",m_displaysInfo[j].edidHash);
        sub.insert("output",m_displaysInfo[j].outputName);
        sub.insert("setable",m_displaysInfo[j].setable);
        sub.insert("brightness",m_displaysInfo[j].rtBrightness);
        arrary.append(sub);
        QDEBUG(j);
    }

    jdoc.setArray(arrary);
    return QString::fromLatin1(jdoc.toJson());
}
