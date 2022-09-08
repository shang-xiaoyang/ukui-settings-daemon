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
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <kwindowsystem.h>
#include <QFile>
#include <QDir>
#include <QtXml>

#include "xrandroutput.h"
#include "save-screen.h"
#include "clib-syslog.h"


#define OUTPUT_ID "outputId"
#define OUTPUT_NAME "name"
#define CRTC_ID "crtc"
#define MODE_ID "modeId"
#define TRANSFORM_CHANGED "transformChanged"
#define OUTPUT_SCALE "scale"
#define OUTPUT_WIDTHMM "widthmm"
#define OUTPUT_HEIGHTMM "heightmm"
#define OUTPUT_WIDTH "width"
#define OUTPUT_HEIGHT "height"
#define OUTPUT_ROTATION "rotation"
#define OUTPUT_RATE "rate"
#define OUTPUT_PRIMARY "primary"
#define OUTPUT_ENABLE "enable"

typedef struct _transform {
    XTransform	    transform;
    const char	    *filter;
    int		    nparams;
    XFixed	    *params;
}Transform_t;


class OutputInfo {
public:
    XRROutputInfo *m_output;
    RRMode m_modeId;
    int x;
    int y;
    int height;
    int width;
    RROutput m_outputId;
};

/*
 * XRR接口中，crtc负责处理显示的方式，output只是一个显示器，output关联到crtc中才会按照预设的方式进行显示。。crtc在什么情况下会关联多个output呢？
 * XRRSetCrtc时，output是传递的数组，就代表其可以控制多个显示器。(需要测试其功能，没有连接的显示器的crtc是0)。。
 * XRRSetCrtcTransform用来处理scale。
 * 	缩放时
 * output->transform.filter = "bilinear";
 * 不缩放时
 * output->transform.filter = "nearest";
*/


double getModeRefresh (const XRRModeInfo *mode_info)
{
    double rate;
    double vTotal = mode_info->vTotal;

    if (mode_info->modeFlags & RR_DoubleScan) {
    /* doublescan doubles the number of lines */
    vTotal *= 2;
    }

    if (mode_info->modeFlags & RR_Interlace) {
    /* interlace splits the frame into two fields */
    /* the field rate is what is typically reported by monitors */
    vTotal /= 2;
    }

    if (mode_info->hTotal && vTotal)
    rate = ((double) mode_info->dotClock /
        ((double) mode_info->hTotal * (double) vTotal));
    else
        rate = 0;
    return rate;
}
SaveScreenParam::SaveScreenParam(QObject *parent): QObject(parent)
{
    Q_UNUSED(parent);

    m_userName = "";

    m_isGet = false;
    m_isSet = false;
}

SaveScreenParam::~SaveScreenParam()
{
    XCloseDisplay(m_pDpy);
    USD_LOG(LOG_DEBUG,".....");
}

void SaveScreenParam::getConfig(){
    QObject::connect(new KScreen::GetConfigOperation(), &KScreen::GetConfigOperation::finished,
                     [&](KScreen::ConfigOperation *op) {

        if (m_MonitoredConfig) {
            if (m_MonitoredConfig->data()) {
                KScreen::ConfigMonitor::instance()->removeConfig(m_MonitoredConfig->data());
                for (const KScreen::OutputPtr &output : m_MonitoredConfig->data()->outputs()) {
                    output->disconnect(this);
                }
                m_MonitoredConfig->data()->disconnect(this);
            }
            m_MonitoredConfig = nullptr;
        }

        m_MonitoredConfig = std::unique_ptr<xrandrConfig>(new xrandrConfig(qobject_cast<KScreen::GetConfigOperation*>(op)->config()));
        m_MonitoredConfig->setValidityFlags(KScreen::Config::ValidityFlag::RequireAtLeastOneEnabledScreen);

        if (false == m_userName.isEmpty()) {
            m_MonitoredConfig->setUserName(m_userName);
        }
        else if (isSet()) {

        } else if (isGet()) {
            m_MonitoredConfig->writeFileForLightDM(false);
            exit(0);
        } else {
            m_MonitoredConfig->writeFile(false);
            exit(0);
        }
    });
}
//先判断是否，可设置为镜像，如果不可设置为镜像则需要设置为拓展。
void SaveScreenParam::setWithoutConfig(OutputsConfig *outputsConfig)
{
    int primaryId;
    bool isSetClone = true;

    primaryId = XRRGetOutputPrimary(m_pDpy, m_rootWindow);
    SYS_LOG(LOG_DEBUG,"primaryId %d  ",primaryId);
    for (int koutput = 0; koutput < m_pScreenRes->noutput; koutput++) {
        XRROutputInfo *outputInfo = XRRGetOutputInfo (m_pDpy, m_pScreenRes, m_pScreenRes->outputs[koutput]);
        if (outputInfo->connection != RR_Connected) {
            SYS_LOG(LOG_DEBUG,"%s skip ",outputInfo->name);
            XRRFreeOutputInfo(outputInfo);
            continue;
        }
        UsdOuputProperty *outputProperty;
        outputProperty = new UsdOuputProperty();
        if ((0==primaryId && 0==koutput )|| (primaryId == (int)m_pScreenRes->outputs[koutput])) {
            outputProperty->setProperty(OUTPUT_PRIMARY, 1);
        } else {
            outputProperty->setProperty(OUTPUT_PRIMARY, 0);
        }
        outputProperty->setProperty(OUTPUT_NAME, outputInfo->name);
        outputProperty->setProperty(CRTC_ID, (int)outputInfo->crtc);
        outputProperty->setProperty(OUTPUT_ID, (int)m_pScreenRes->outputs[koutput]);
        outputProperty->setProperty("npreferred",(int)outputInfo->npreferred);
        outputProperty->setProperty(OUTPUT_WIDTHMM, (int)outputInfo->mm_width);
        outputProperty->setProperty(OUTPUT_HEIGHTMM, (int)outputInfo->mm_height);

        if (0 == outputInfo->npreferred) {
            outputProperty->setProperty(MODE_ID, (int)outputInfo->modes[0]);//识别不出最佳模式
        } else {
            outputProperty->setProperty(MODE_ID, (int)outputInfo->modes[0]);
        }

        for (int kmode = 0; kmode < outputInfo->nmode; ++kmode) {
            UsdOutputMode mode;
            mode.m_modeId = outputInfo->modes[kmode];
            for (int kresMode = 0; kresMode < m_pScreenRes->nmode; ++kresMode) {
                XRRModeInfo *resMode = &m_pScreenRes->modes[kresMode];
                if (mode.m_modeId == resMode->id) {
                    mode.m_height = resMode->height;
                    mode.m_width = resMode->width;
                    mode.m_rate =  getModeRefresh(resMode);
                    break;
                }
            }
            outputProperty->addMode(mode);
        }

        if (outputsConfig->m_dpi == "192") {
            outputProperty->setProperty(OUTPUT_SCALE, getScaleWithSize( outputProperty->m_modes[0].m_width,
                                                                        outputProperty->m_modes[0].m_height,
                                                                        outputInfo->mm_width,
                                                                        outputInfo->mm_height));
        } else {
            outputProperty->setProperty(OUTPUT_SCALE, 1);
        }

        outputProperty->setProperty(OUTPUT_ROTATION, 1);
        outputProperty->setProperty(MODE_ID, outputProperty->m_modes[0].m_modeId);
        outputProperty->setProperty(OUTPUT_WIDTH, outputProperty->m_modes[0].m_width);
        outputProperty->setProperty(OUTPUT_HEIGHT, outputProperty->m_modes[0].m_height);
        outputProperty->setProperty(OUTPUT_RATE, outputProperty->m_modes[0].m_rate);
        outputsConfig->m_outputList.append(outputProperty);
        XRRFreeOutputInfo(outputInfo);
    }

    if (outputsConfig->m_outputList.count()<2) {
        return;
    }

    //find all output clonemode
    for (int koutput = 1; koutput < outputsConfig->m_outputList.count(); koutput++) {
        QList<UsdOutputMode> qlistMode;
        if (koutput==1) {
            qlistMode = outputsConfig->m_outputList[0]->m_modes;//前两个屏幕从modes里面挑选共有分辨率到cloneModes
        } else {
            qlistMode = outputsConfig->m_outputList[0]->m_cloneModes;//第三个屏幕之后从交集内取。
        }
        for (int firstScreenMode = 0; firstScreenMode < qlistMode.count(); firstScreenMode++){
            for (int screenMode = 0; screenMode < outputsConfig->m_outputList[koutput]->m_modes.count(); screenMode++){
                UsdOutputMode fMode = qlistMode[firstScreenMode];
                UsdOutputMode sMode = outputsConfig->m_outputList[koutput]->m_modes[screenMode];

                if (fMode.m_height == sMode.m_height && fMode.m_width == sMode.m_width) {
                    outputsConfig->m_outputList[0]->updateCloneMode(fMode);
                    outputsConfig->m_outputList[screenMode]->updateCloneMode(sMode);
                }
            }
        }
        if(0 == outputsConfig->m_outputList[koutput]->m_cloneModes.count()) {
            isSetClone = false;
            break;
        }

    }

    //可设置为镜像模式
    if (isSetClone) {
        int maxCloneWidth = 0;
        int maxCloneHeight = 0;
        double rate = 0.0;
        int modeId = 0;
        for (int koutput = outputsConfig->m_outputList.count()-1; koutput >= 0; koutput--) {
            rate = 0.0;

            if (outputsConfig->m_dpi == "192") {
                outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_SCALE, 2);
            } else {
                outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_SCALE, 1);
            }
            if (koutput == outputsConfig->m_outputList.count()-1) {
                for (int kmode = 0;  kmode < outputsConfig->m_outputList[koutput]->m_cloneModes.count(); kmode++) {
                    if (outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_height * outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_width >=
                            maxCloneHeight*maxCloneWidth && outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_rate > rate) {
                        modeId = outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_modeId;
                        maxCloneHeight = outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_height;
                        maxCloneWidth = outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_width;
                        rate = outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_rate;
                    }
                }
                outputsConfig->m_outputList[koutput]->setProperty(MODE_ID, modeId);
                outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_HEIGHT, maxCloneHeight);
                outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_WIDTH, maxCloneWidth);
                outputsConfig->m_outputList[koutput]->setProperty("x",0);
                outputsConfig->m_outputList[koutput]->setProperty("y", 0);
            } else {
                for (int kmode = 0;  kmode < outputsConfig->m_outputList[koutput]->m_cloneModes.count(); kmode++) {
                    if (outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_height * outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_width ==
                            maxCloneHeight*maxCloneWidth && outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_rate > rate) {
                        modeId = outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_modeId;
                        maxCloneHeight = outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_height;
                        maxCloneWidth = outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_width;
                        rate = outputsConfig->m_outputList[koutput]->m_cloneModes[kmode].m_rate;

                        outputsConfig->m_outputList[koutput]->setProperty(MODE_ID, modeId);
                        outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_HEIGHT, maxCloneHeight);
                        outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_WIDTH, maxCloneWidth);
                        outputsConfig->m_outputList[koutput]->setProperty("x",0);
                        outputsConfig->m_outputList[koutput]->setProperty("y", 0);
                    }
                }
            }
        }
        return;
    }

    //设置为拓展模式
    for (int koutput = outputsConfig->m_outputList.count() - 1; koutput >= 0; koutput) {
        int modeId = outputsConfig->m_outputList[koutput]->m_modes[0].m_modeId;
        int width = outputsConfig->m_outputList[koutput]->m_modes[0].m_width;
        int height = outputsConfig->m_outputList[koutput]->m_modes[0].m_height;
        int widthmm = outputsConfig->m_outputList[koutput]->property(OUTPUT_WIDTHMM).toInt();
        int heightmm = outputsConfig->m_outputList[koutput]->property(OUTPUT_HEIGHTMM).toInt();
        double scale = getScaleWithSize(width, height, widthmm, heightmm);

        outputsConfig->m_outputList[koutput]->setProperty(MODE_ID, modeId);
        outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_HEIGHT, height * scale);
        outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_WIDTH, width * scale);
        outputsConfig->m_outputList[koutput]->setProperty("x", outputsConfig->m_screenWidth);
        outputsConfig->m_outputList[koutput]->setProperty("y", 0);
        outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_SCALE, scale);
        outputsConfig->m_screenWidth += width * scale;
    }
}



void SaveScreenParam::setUserConfigParam()
{
    if (false ==initXparam()) {
        exit(0);
    }

    readKscreenConfigAndSetItWithX(getKscreenConfigFullPathInLightDM());
    exit(0);
}

void SaveScreenParam::setUserName(QString str)
{
    m_userName = str;
}

void SaveScreenParam::setScreenSize()
{
    int screenInt = DefaultScreen (m_pDpy);
    if (m_kscreenConfigParam.m_screenWidth != DisplayWidth(m_pDpy, screenInt) ||
            m_kscreenConfigParam.m_screenHeight != DisplayHeight(m_pDpy, screenInt)) {
        int fb_width_mm;
        int fb_height_mm;

        double dpi = (25.4 * DisplayHeight(m_pDpy, screenInt)) / DisplayHeightMM(m_pDpy,screenInt);
        fb_width_mm = (25.4 * m_kscreenConfigParam.m_screenWidth) /dpi;
        fb_height_mm = (25.4 * m_kscreenConfigParam.m_screenHeight) /dpi;
        //dpi = Dot Per Inch，一英寸是2.54cm即25.4mm
        XRRSetScreenSize(m_pDpy, m_rootWindow, m_kscreenConfigParam.m_screenWidth, m_kscreenConfigParam.m_screenHeight,
                        fb_width_mm, fb_height_mm);
    }
}

void SaveScreenParam::readKscreenConfigAndSetItWithX(QString kscreenConfigName)
{
    QFile file;
    char *dpi;
    QString configFullPath = kscreenConfigName;
    dpi = XGetDefault(m_pDpy, "Xft", "dpi");
    m_kscreenConfigParam.m_dpi = "96";

    if (dpi != nullptr) {
        m_kscreenConfigParam.m_dpi = QString::fromLatin1(dpi);
    }

    if (!QFile::exists(kscreenConfigName)) {
        setWithoutConfig(&m_kscreenConfigParam);
        debugAllOutput(&m_kscreenConfigParam);
        SYS_LOG(LOG_ERR,"can't open %s's screen config>%s set it without config.",m_userName.toLatin1().data(),configFullPath.toLatin1().data());
    } else {
        if (false == readKscreenConfig(&m_kscreenConfigParam, kscreenConfigName)) {
            setWithoutConfig(&m_kscreenConfigParam);
            SYS_LOG(LOG_ERR,"can't open %s's screen config>%s",m_userName.toLatin1().data(),configFullPath.toLatin1().data());
        }
    }

    clearAndGetCrtcs(&m_kscreenConfigParam);
    setScreenSize();
    setCrtcConfig(&m_kscreenConfigParam);
    return ;
}

void SaveScreenParam::disableCrtc()
{
    int tempInt = 0;

    if (false == initXparam()) {
        return;
    }

    for (tempInt = 0; tempInt < m_pScreenRes->ncrtc; tempInt++) {
        int ret = 0;
        ret = XRRSetCrtcConfig (m_pDpy, m_pScreenRes, m_pScreenRes->crtcs[tempInt], CurrentTime,
                                0, 0, None, RR_Rotate_0, NULL, 0);
        if (ret != RRSetConfigSuccess) {
            SYS_LOG(LOG_ERR,"disable crtc:%d error! ");
        }
    }

}

void SaveScreenParam::readConfigAndSetBak()
{
    if (m_MonitoredConfig->lightdmFileExists()) {
        std::unique_ptr<xrandrConfig> monitoredConfig = m_MonitoredConfig->readFile(false);

        if (monitoredConfig == nullptr ) {
            USD_LOG(LOG_DEBUG,"config a error");
            exit(0);
            return;
        }

        m_MonitoredConfig = std::move(monitoredConfig);
        if (m_MonitoredConfig->canBeApplied()) {
            connect(new KScreen::SetConfigOperation(m_MonitoredConfig->data()),
                    &KScreen::SetConfigOperation::finished,
                    this, [this]() {
                USD_LOG(LOG_DEBUG,"set success。。");
                exit(0);
            });
        } else {
            USD_LOG(LOG_ERR,"--|can't be apply|--");
            exit(0);
        }
    } else {
         exit(0);
    }
}

QString SaveScreenParam::printUserConfigParam()
{
    if (false ==initXparam()) {
        exit(0);
    }
    QFile file;
    char *dpi;
    QString configFullPath = getKscreenConfigFullPathInLightDM();
    dpi = XGetDefault(m_pDpy, "Xft", "dpi");
    m_kscreenConfigParam.m_dpi = "96";

    if (dpi != nullptr) {
        m_kscreenConfigParam.m_dpi = QString::fromLatin1(dpi);
    }

    if (!QFile::exists(configFullPath)) {
        setWithoutConfig(&m_kscreenConfigParam);
        debugAllOutput(&m_kscreenConfigParam);
        SYS_LOG(LOG_ERR,"can't open %s's screen config>%s set it without config.",m_userName.toLatin1().data(),configFullPath.toLatin1().data());
    } else {
        if (false == readKscreenConfig(&m_kscreenConfigParam, configFullPath)) {
            setWithoutConfig(&m_kscreenConfigParam);
            SYS_LOG(LOG_ERR,"can't open %s's screen config>%s",m_userName.toLatin1().data(),configFullPath.toLatin1().data());
        }
    }

    return showAllOutputInJson(&m_kscreenConfigParam);
}

//暂时不使用
void SaveScreenParam::setXdpi(double scale)
{
    Display    *dpy;
//    QString str = QString("Xft.dpi:\t%1\n")
//                         .arg(192);

    QString str = QString("Xft.dpi:\t%1\nXcursor.size:\t%2\nXcursor.theme:\t%3\n")
                         .arg(scale*96)
                         .arg(48)
                         .arg("dark-sense");

    dpy = XOpenDisplay(NULL);
    XChangeProperty(dpy, RootWindow(dpy, 0), XA_RESOURCE_MANAGER, XA_STRING, 8,
                    PropModeReplace, (unsigned char *) str.toLatin1().data(), str.length());
    XCloseDisplay(dpy);
    USD_LOG(LOG_DEBUG,"%s",str.toLatin1().data());
}

void SaveScreenParam::getRootWindows()
{

}

void SaveScreenParam::getScreen()
{
    //    m_res = XRRGetScreenResourcesCurrent (m_dpy, root);
}

void SaveScreenParam::debugAllOutput(OutputsConfig *outputsConfig)
{
    SYS_LOG(LOG_DEBUG,"find %d outputs",outputsConfig->m_outputList.count());
    for (int koutput = 0; koutput < outputsConfig->m_outputList.count(); koutput++) {
        int modeId  = outputsConfig->m_outputList[koutput]->property(MODE_ID).toInt();
        int width = outputsConfig->m_outputList[koutput]->property(OUTPUT_WIDTH).toInt();
        int height = outputsConfig->m_outputList[koutput]->property(OUTPUT_HEIGHT).toInt();
        int x = outputsConfig->m_outputList[koutput]->property("x").toInt();
        int y = outputsConfig->m_outputList[koutput]->property("y").toInt();
        int crtc = outputsConfig->m_outputList[koutput]->property(CRTC_ID).toInt();
        double scale = outputsConfig->m_outputList[koutput]->property(OUTPUT_SCALE).toDouble();
        double rate = outputsConfig->m_outputList[koutput]->property(OUTPUT_RATE).toDouble();
        QString outputName = outputsConfig->m_outputList[koutput]->property(OUTPUT_NAME).toString();

        SYS_LOG(LOG_DEBUG,"%s: usd mode(%d) %dx%d@%f at(%dx%d) in crtc(%d) scale:%f",outputName.toLatin1().data(), modeId, width, height, rate, x, y,
                crtc, scale);

    }
}

int SaveScreenParam::initXparam()
{
    m_pDpy = XOpenDisplay (m_pDisplayName);
    if (m_pDpy == NULL) {
        SYS_LOG(LOG_DEBUG,"XOpenDisplay fail...");
        return false;
    }

    m_screen = DefaultScreen(m_pDpy);
    if (m_screen >= ScreenCount (m_pDpy)) {
        SYS_LOG(LOG_DEBUG,"Invalid screen number %d (display has %d)",m_screen, ScreenCount(m_pDpy));
        return false;
    }

    m_rootWindow = RootWindow(m_pDpy, m_screen);

    m_pScreenRes = XRRGetScreenResources(m_pDpy, m_rootWindow);
    if (NULL == m_pScreenRes) {
        SYS_LOG(LOG_DEBUG,"could not get screen resources",m_screen, ScreenCount(m_pDpy));
        return false;
    }

    if (m_pScreenRes->noutput == 0) {
        SYS_LOG(LOG_DEBUG, "noutput is 0!!");
        return false;
    }

    SYS_LOG(LOG_DEBUG,"initXparam success");
    return true;
}

void SaveScreenParam::clearAndGetCrtcs(OutputsConfig *outputsConfig)
{
    QList<int> hadUseCrtcList;

    for (int koutput = 0; koutput < outputsConfig->m_outputList.count(); koutput++) {
        double scale = outputsConfig->m_outputList[koutput]->property(OUTPUT_SCALE).toDouble();

        for (int tempInt = 0; tempInt < m_pScreenRes->noutput; tempInt++) {
            XRROutputInfo	*outputInfo = XRRGetOutputInfo (m_pDpy, m_pScreenRes, m_pScreenRes->outputs[tempInt]);
            QString outputName = QString::fromLatin1(outputInfo->name);
            if (outputInfo->connection != RR_Connected ||
                    outputName != outputsConfig->m_outputList[koutput]->property(OUTPUT_NAME).toString()) {
                XRRFreeOutputInfo(outputInfo);
                continue;
            }
            outputsConfig->m_outputList[koutput]->setProperty(OUTPUT_ID, (int)m_pScreenRes->outputs[tempInt]);
            if (0 != outputInfo->crtc) {
                hadUseCrtcList.append(outputInfo->crtc);
                outputsConfig->m_outputList[koutput]->setProperty(CRTC_ID, (int)outputInfo->crtc);
                if (checkTransformat(outputInfo->crtc, scale)) {
                    clearCrtc(outputInfo->crtc);
                    outputsConfig->m_outputList[koutput]->setProperty(TRANSFORM_CHANGED, 1);
                }
            } else {
                for (int kcrtc = 0; kcrtc < m_pScreenRes->ncrtc; kcrtc++) {
                    XRRCrtcInfo *crtcInfo = XRRGetCrtcInfo(m_pDpy, m_pScreenRes, m_pScreenRes->crtcs[kcrtc]);

                    if (0 == crtcInfo->noutput) {
                        if (!hadUseCrtcList.contains(m_pScreenRes->crtcs[kcrtc])) {
                            hadUseCrtcList.append(m_pScreenRes->crtcs[kcrtc]);
                            outputsConfig->m_outputList[koutput]->setProperty(CRTC_ID, (int)m_pScreenRes->crtcs[kcrtc]);
                            XRRFreeCrtcInfo(crtcInfo);
                            break;
                        }
                    }
                    XRRFreeCrtcInfo(crtcInfo);
                }
            }
            XRRFreeOutputInfo(outputInfo);
        }
    }
}

bool SaveScreenParam::checkTransformat(RRCrtc crtc, double scale)
{
    XRRCrtcTransformAttributes  *attr;
    XTransform  transform;
    if (XRRGetCrtcTransform (m_pDpy, crtc, &attr) && attr) {

        for (int x = 0; x < 3; x++) {
            for (int x1 = 0; x1 < 3; x1++) {
                transform.matrix[x][x1] = 0;
            }
        }

        syslog(LOG_ERR, "USD: ------- scale 2========= %f", scale);
        transform.matrix[0][0] = XDoubleToFixed(scale); // /scale;
        transform.matrix[1][1] = XDoubleToFixed(scale); // /scale;
        transform.matrix[2][2] = XDoubleToFixed(1.0);
        XFree (attr);

        if (attr->currentTransform.matrix[0][0] == transform.matrix[0][0] &&
                attr->currentTransform.matrix[1][1] == transform.matrix[1][1] &&
                attr->currentTransform.matrix[2][2] == transform.matrix[2][2]) {
            return false;
        }
        SYS_LOG(LOG_DEBUG,"%d,%d,%d,%d,%d,%d", transform.matrix[0][0], transform.matrix[1][1]
                ,transform.matrix[2][2],attr->currentTransform.matrix[0][0],attr->currentTransform.matrix[1][1],attr->currentTransform.matrix[2][2]);
    }

    return true;
}

bool SaveScreenParam::clearCrtc(RRCrtc crtc)
{
    int ret;
    ret = XRRSetCrtcConfig (m_pDpy, m_pScreenRes, crtc, CurrentTime,
                            0, 0, None, RR_Rotate_0, NULL, 0);
    if (ret != RRSetConfigSuccess) {
        SYS_LOG(LOG_ERR,"disable crtc:%d error! ");
        return false;
    }

    return true;
}

void SaveScreenParam::setCrtcConfig(OutputsConfig *outputsConfig)
{

    for (int klist = 0; klist < outputsConfig->m_outputList.count(); klist++) {
        for (int tempInt = 0; tempInt < m_pScreenRes->noutput; tempInt++) {
            XRROutputInfo	*outputInfo = XRRGetOutputInfo (m_pDpy, m_pScreenRes, m_pScreenRes->outputs[tempInt]);
            QString outputName = QString::fromLatin1(outputInfo->name);
            if (outputInfo->connection != RR_Connected ||
                    outputName != outputsConfig->m_outputList[klist]->property(OUTPUT_NAME).toString()) {
                XRRFreeOutputInfo(outputInfo);
                continue;
            }

            RRMode modeID = getModeId(outputInfo,  outputsConfig->m_outputList[klist]);
            double scale = outputsConfig->m_outputList[klist]->property(OUTPUT_SCALE).toInt();
            int x = outputsConfig->m_outputList[klist]->property("x").toInt() * scale;
            int y =  outputsConfig->m_outputList[klist]->property("y").toInt() * scale;
            int crtc = outputsConfig->m_outputList[klist]->property(CRTC_ID).toInt();
            int outputId =  outputsConfig->m_outputList[klist]->property(OUTPUT_ID).toInt();
            int rotationAngle = outputsConfig->m_outputList[klist]->property(OUTPUT_ROTATION).toInt();
            int transformatchanged = outputsConfig->m_outputList[klist]->property(TRANSFORM_CHANGED).toInt();

            if (transformatchanged) {
                XRRCrtcTransformAttributes  *attr;
                XTransform  transform;
                char *filter;
                XFixed *xfixed;
                if (XRRGetCrtcTransform (m_pDpy,crtc, &attr) && attr) {
                    for (int x = 0; x < 3; x++) {
                        for (int x1 = 0; x1 < 3; x1++) {
                            transform.matrix[x][x1] = 0;
                        }
                    }
                    syslog(LOG_ERR, "USD: ------- SCALE 1 ========= %f", scale);
                    transform.matrix[0][0] = XDoubleToFixed(scale);// /scale;
                    transform.matrix[1][1] = XDoubleToFixed(scale);// /scale;
                    transform.matrix[2][2] = XDoubleToFixed (1.0);

                    if (scale == 1.0) {
                           filter = "nearest";
                    } else {
                           filter = "bilinear";
                    }
                    SYS_LOG(LOG_DEBUG,"USD: %f,%d,%d,%d",scale,
                            transform.matrix[0][0],transform.matrix[1][1],
                            transform.matrix[2][2]);
                    XRRSetCrtcTransform(m_pDpy, crtc, &transform, filter, xfixed, 0);
                    XFree (attr);
                }
            }
            int ret = XRRSetCrtcConfig (m_pDpy, m_pScreenRes, crtc, CurrentTime,
                                    x, y, modeID, rotationAngle, (RROutput*)&outputId, 1);

            SYS_LOG(LOG_DEBUG,"%s(%d) usd mode:%d at %dx%d rotate:%d",outputInfo->name, outputId, modeID, x, y, rotationAngle);
            if (ret != RRSetConfigSuccess) {
                SYS_LOG(LOG_ERR,"%s RRSetConfigFail..",m_kscreenConfigParam.m_outputList[klist]->property(OUTPUT_NAME).toString().toLatin1().data());
            } else {
                SYS_LOG(LOG_DEBUG,"%s RRSetConfigSuccess",m_kscreenConfigParam.m_outputList[klist]->property(OUTPUT_NAME).toString().toLatin1().data());

            }
            XRRFreeOutputInfo(outputInfo);
        }
    }
}

QString SaveScreenParam::showAllOutputInJson(OutputsConfig *outputsConfig)
{
    QJsonArray arrary;
    QJsonDocument jdoc;

    for (int j = 0; j < outputsConfig->m_outputList.count(); j++) {
        QJsonObject sub;
        QString name = outputsConfig->m_outputList[j]->property(OUTPUT_NAME).toString();
        int width = outputsConfig->m_outputList[j]->property(OUTPUT_WIDTH).toInt();
        int height = outputsConfig->m_outputList[j]->property(OUTPUT_HEIGHT).toInt();
        int x = outputsConfig->m_outputList[j]->property("x").toInt();
        int y = outputsConfig->m_outputList[j]->property("y").toInt();
        double scale = outputsConfig->m_outputList[j]->property(OUTPUT_SCALE).toDouble();
        double rate = outputsConfig->m_outputList[j]->property(OUTPUT_RATE).toDouble();

        sub.insert("name",name);
        sub.insert("width",width);
        sub.insert("height",height);
        sub.insert("rate",rate);
        sub.insert("x",x);
        sub.insert("x",y);
        sub.insert("scale", scale);
        arrary.append(sub);

    }

    jdoc.setArray(arrary);
    return QString::fromLatin1(jdoc.toJson());
}

double SaveScreenParam::getScaleWithSize(int width, int height, int widthmm, int heightmm)
{
    double inch = 0.0;
    double scale = 0.0;

    double screenArea  = width * height;
    inch = sqrt(widthmm * widthmm + heightmm * heightmm) / (25.4);

//    qSqrt(qPow(width,2) + qPow(height,2)) * 0.3937;

    if (inch <= 10.00) {
        scale = qSqrt(screenArea) / qSqrt(1024 * 576);
    }
    else if (10.00 < inch && inch <= 15.00) { // 10 < inch <= 15 : 1366x768
        scale = qSqrt(screenArea) / qSqrt(1366 * 768);
    }
    else if (15.00 < inch && inch <= 20.00) { // 15 < inch <= 20 : 1600x900
        scale = qSqrt(screenArea) / qSqrt(1600 * 900);
    }
    else if (20.00 < inch && inch <= 30.00) { // 20 < inch <= 30 : 1920x1080
        scale = qSqrt(screenArea) / qSqrt(1920 * 1080);
    }
    else if (30 < inch && inch<= 60) { // 30 < inch <= 60 :
        scale = qSqrt(screenArea) / qSqrt(1600 * 900);
    }
    else { // inch > 60
        scale = qSqrt(screenArea) / qSqrt(1280 * 720);
    }

    syslog(LOG_ERR, "USD: ----- width:%d,height:%d,widthmm:%d,height:%d,inch:%lf,scale:%lf",width,height,widthmm, heightmm, inch, scale);
    return getScale(scale);//scale;
}

QString SaveScreenParam::getKscreenConfigFullPathInLightDM()
{
    int             actualFormat;
    int             tempInt;

    uchar           *prop;

    Atom            tempAtom;
    Atom            actualType;

    ulong           nitems;
    ulong           bytesAfter;
    QStringList     outputsHashList;

    tempAtom = XInternAtom (m_pDpy, "EDID", false);
    for (tempInt = 0; tempInt < m_pScreenRes->noutput; tempInt++) {
       XRRGetOutputProperty(m_pDpy, m_pScreenRes->outputs[tempInt], tempAtom,
                 0, 100, False, False,
                 AnyPropertyType,
                 &actualType, &actualFormat,
                 &nitems, &bytesAfter, &prop);

       QCryptographicHash hash(QCryptographicHash::Md5);
       if (nitems == 0) {
           XRROutputInfo	*outputInfo = XRRGetOutputInfo (m_pDpy, m_pScreenRes, m_pScreenRes->outputs[tempInt]);
           if (RR_Connected == outputInfo->connection) {
               SYS_LOG(LOG_DEBUG,"%s %d edid is empty",outputInfo->name,outputInfo->connection);
               QString checksum = QString::fromLatin1(outputInfo->name);
               qDebug()<<checksum;
               outputsHashList.append(checksum);
           }
           continue;
       }

       hash.addData(reinterpret_cast<const char *>(prop), nitems);
       QString checksum = QString::fromLatin1(hash.result().toHex());
       outputsHashList.append(checksum);
    }

    if (outputsHashList.count() == 0) {
        SYS_LOG(LOG_DEBUG,"outputsHashList is empty");
        return "";
    }
    std::sort(outputsHashList.begin(), outputsHashList.end());
    const auto configHash = QCryptographicHash::hash(outputsHashList.join(QString()).toLatin1(),
                                               QCryptographicHash::Md5);

    m_KscreenConfigFile = QString::fromLatin1(configHash.toHex());

    if (m_userName.isEmpty()) {
        return "";
    }

    return QString("/var/lib/lightdm-data/%1/usd/kscreen/%2").arg(m_userName).arg(QString::fromLatin1(configHash.toHex()));
}

//所有显示器的模式都放在一起，需要甄别出需要的显示器，避免A取到B的模式。需要判断那个显示器支持那些模式
RRMode SaveScreenParam::getModeId(XRROutputInfo	*outputInfo,UsdOuputProperty *kscreenOutputParam)
{
    double rate;
    double vTotal;

    RRMode ret = 0;

    for (int m = 0; m < m_pScreenRes->nmode; m++) {
        XRRModeInfo *mode = &m_pScreenRes->modes[m];
        vTotal = mode->vTotal;
//        SYS_LOG(LOG_DEBUG,"start check mode:%s id:%d for %s", mode->name, mode->id, outputInfo->name);
        if (mode->modeFlags & RR_DoubleScan) {
            /* doublescan doubles the number of lines */
            vTotal *= 2;
        }

        if (mode->modeFlags & RR_Interlace) {
            /* interlace splits the frame into two fields */
            /* the field rate is what is typically reported by monitors */
            vTotal /= 2;
        }
        rate = mode->dotClock / ((double) mode->hTotal * (double) vTotal);
        if (mode->width == kscreenOutputParam->property(OUTPUT_WIDTH).toUInt()
                && mode->height == kscreenOutputParam->property(OUTPUT_HEIGHT).toUInt()) {
            double kscreenRate = kscreenOutputParam->property(OUTPUT_RATE).toDouble();
            if (qAbs(kscreenRate - rate) < 0.02) {
                for (int k = 0; k< outputInfo->nmode; k++) {
                    if (outputInfo->modes[k] == mode->id) {
//                        SYS_LOG(LOG_DEBUG,"find %s mode:%s id:%d refresh:%f",outputInfo->name, mode->name, mode->id, rate);
                        return mode->id;
                    }
                }
            } else {
//                SYS_LOG(LOG_DEBUG,"%dx%d %f!=%f",mode->width,mode->height,rate,kscreenRate);
            }
        }
    }

    return ret;
}

bool SaveScreenParam::readKscreenConfig(OutputsConfig *outputsConfig, QString configFullPath)
{
    QFile file;
    QJsonDocument parser;
    QVariantList outputsInfo;

    file.setFileName(configFullPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    outputsInfo = parser.fromJson(file.readAll()).toVariant().toList();
    for (const auto &variantInfo : outputsInfo) {
        UsdOuputProperty *outputProperty;
        outputProperty = new UsdOuputProperty();

        const QVariantMap info = variantInfo.toMap();
        const QVariantMap posInfo = info[QStringLiteral("pos")].toMap();
        const QVariantMap mateDataInfo = info[QStringLiteral("metadata")].toMap();
        const QVariantMap modeInfo = info[QStringLiteral("mode")].toMap();//接入双屏，关闭一屏幕时的处理？
        const QVariantMap sizeInfo = modeInfo[QStringLiteral("size")].toMap();
        double scale = 0;

        outputProperty->setProperty("x", posInfo[QStringLiteral("x")].toString());
        outputProperty->setProperty("y", posInfo[QStringLiteral("y")].toString());
        outputProperty->setProperty(OUTPUT_WIDTH, sizeInfo[QStringLiteral("width")].toString());
        outputProperty->setProperty(OUTPUT_HEIGHT, sizeInfo[QStringLiteral("height")].toString());
        outputProperty->setProperty(OUTPUT_NAME, mateDataInfo[QStringLiteral("name")].toString());
        outputProperty->setProperty(OUTPUT_PRIMARY, info[QStringLiteral("primary")].toString());
        outputProperty->setProperty(OUTPUT_ROTATION, info[QStringLiteral("rotation")].toString());
        outputProperty->setProperty(OUTPUT_RATE, modeInfo[QStringLiteral("refresh")].toString());
        outputProperty->setProperty(OUTPUT_ENABLE, info[QStringLiteral("enabled")].toString());
        outputProperty->setProperty("seted", false);
        outputProperty->setProperty(OUTPUT_SCALE, info[QStringLiteral("scale")].toDouble());
        scale = outputProperty->getscale();

        if (info[QStringLiteral("enabled")].toString() == "true") {
            if (outputProperty->property(OUTPUT_ROTATION).toString() == "8" ||
                    outputProperty->property(OUTPUT_ROTATION).toString() == "2") {
                if (outputProperty->property(OUTPUT_HEIGHT).toInt()*(1/scale) + outputProperty->property("x").toInt() > outputsConfig->m_screenWidth) {
                    outputsConfig->m_screenWidth = outputProperty->property(OUTPUT_HEIGHT).toInt()*(1/scale) + outputProperty->property("x").toInt();
                }

                if (outputProperty->property(OUTPUT_WIDTH).toInt()*(1/scale) + outputProperty->property("y").toInt() > outputsConfig->m_screenHeight) {
                    outputsConfig->m_screenHeight = outputProperty->property(OUTPUT_WIDTH).toInt()*(1/scale) + outputProperty->property("y").toInt();
                }
            }
            else{
                if (outputProperty->property(OUTPUT_WIDTH).toInt()*(1/scale) + outputProperty->property("x").toInt() > outputsConfig->m_screenWidth) {
                    outputsConfig->m_screenWidth = outputProperty->property(OUTPUT_WIDTH).toInt()*(1/scale) + outputProperty->property("x").toInt();
                }

                if (outputProperty->property(OUTPUT_HEIGHT).toInt()*(1/scale) + outputProperty->property("y").toInt() > outputsConfig->m_screenHeight) {
                    outputsConfig->m_screenHeight = outputProperty->property(OUTPUT_HEIGHT).toInt()*(1/scale) + outputProperty->property("y").toInt();
                }
            }
            outputsConfig->m_outputList.append(outputProperty);
        } else {
//            USD_LOG_SHOW_PARAMS(outputProperty->property(OUTPUT_ENABLE).toString().toLatin1().data());
        }
    }
}


double SaveScreenParam::getScoreScale(double scaling)
{
    double scale = 0.0;
    if (scaling <= 1.15)
        scale = 1;
    else if (scaling <= 1.4)
        scale = 1.25;
    else if (scaling <= 1.65)
        scale = 1.5;
    else if (scaling <= 1.9)
        scale = 1.75;
    else
        scale = 2;

    return scale;
}

double SaveScreenParam::getScale(double scaling)
{
    double scale = 0.0;
    if (scaling <= 2.15)
        scale = getScoreScale(scaling);
    else if (scaling <= 3.15)
        scale = getScoreScale(scaling - 1) + 1;
    else if (scaling <= 4.15)
        scale = getScoreScale(scaling - 2) + 2;
    else if (scaling <= 5.15)
        scale = getScoreScale(scaling - 3) + 3;
    else if (scaling <= 6.15)
        scale = getScoreScale(scaling -4 ) + 4;
    else
        scale = 6;// 根据目前大屏及8K屏幕，最高考虑6倍缩放

    return scale;
}


