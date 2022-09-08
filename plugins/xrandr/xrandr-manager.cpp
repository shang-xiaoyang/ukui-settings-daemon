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
#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <QProcess>
#include <QX11Info>
#include <QtXml>
#include <QtConcurrent/QtConcurrent>

#include "xrandr-manager.h"

#include <QOrientationReading>
#include <memory>


#include <QDBusMessage>

extern "C"{
#include <glib.h>
//#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/Xrandr.h>
#include <xorg/xserver-properties.h>
#include <gudev/gudev.h>
#include "clib-syslog.h"
#include <libudev.h>
}

#define SETTINGS_XRANDR_SCHEMAS     "org.ukui.SettingsDaemon.plugins.xrandr"
#define XRANDR_ROTATION_KEY         "xrandr-rotations"
#define XRANDR_PRI_KEY              "primary"
#define XSETTINGS_SCHEMA            "org.ukui.SettingsDaemon.plugins.xsettings"
#define XSETTINGS_KEY_SCALING       "scaling-factor"

#define MAX_SIZE_MATCH_DIFF         0.05

#define MAP_CONFIG "/.config/touchcfg.ini"
#define MONITOR_NULL_SERIAL "kydefault"

unsigned char *getDeviceNode (XIDeviceInfo devinfo);
typedef struct
{
    unsigned char *input_node;
    XIDeviceInfo dev_info;
}TsInfo;

XrandrManager::XrandrManager():
    m_outputsChangedSignal(eScreenSignal::isNone),
    m_acitveTimer(new QTimer(this)),
    m_outputsInitTimer(new QTimer(this)),
    m_offUsbScreenTimer(new QTimer(this)),
    m_onUsbScreenTimer(new QTimer(this))
{
    KScreen::Log::instance();
    m_xrandrDbus = new xrandrDbus(this);
    m_xrandrSettings = new QGSettings(SETTINGS_XRANDR_SCHEMAS);

    new XrandrAdaptor(m_xrandrDbus);
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (sessionBus.registerService(DBUS_XRANDR_NAME)) {
        sessionBus.registerObject(DBUS_XRANDR_PATH,
                                  m_xrandrDbus,
                                  QDBusConnection::ExportAllContents);
    } else {
        USD_LOG(LOG_ERR, "register dbus error");
    }

    m_ukccDbus = new QDBusInterface("org.ukui.ukcc.session",
                                   "/",
                                   "org.ukui.ukcc.session.interface",
                                   QDBusConnection::sessionBus());

    m_outputModeEnum = QMetaEnum::fromType<UsdBaseClass::eScreenMode>();

    m_statusManagerDbus = new QDBusInterface("com.kylin.statusmanager.interface","/","com.kylin.statusmanager.interface",QDBusConnection::sessionBus(),this);

    if (m_statusManagerDbus->isValid()) {
        connect(m_statusManagerDbus, SIGNAL(mode_change_signal(bool)),this,SLOT(doTabletModeChanged(bool)));
        connect(m_statusManagerDbus, SIGNAL(rotations_change_signal(QString)),this,SLOT(doRotationChanged(QString)));
    } else {
        USD_LOG(LOG_ERR, "m_statusManagerDbus error");
    }

    m_onUsbScreenTimer->setSingleShot(true);
    m_offUsbScreenTimer->setSingleShot(true);

    connect(m_offUsbScreenTimer, &QTimer::timeout, this, [=](){
        std::unique_ptr<xrandrConfig> MonitoredConfig = m_outputsConfig->readFile(false);

        if (MonitoredConfig == nullptr ) {
            USD_LOG(LOG_DEBUG,"config a error");
            setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::cloneScreenMode));
            return;
        }

        m_outputsConfig = std::move(MonitoredConfig);
        applyConfig();
    });

    connect(m_onUsbScreenTimer, &QTimer::timeout, this, [=](){
         setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::extendScreenMode));
    });

    connect(m_xrandrDbus,&xrandrDbus::controlScreen,this,&XrandrManager::doCalibrate);
}

void XrandrManager::getInitialConfig()
{
    static bool getConfigFinish = false;

    if (!getConfigFinish) {
        USD_LOG(LOG_DEBUG,"start 1500 timer...");
        m_outputsInitTimer->start(1500);
    }

    connect(new KScreen::GetConfigOperation, &KScreen::GetConfigOperation::finished,
            this, [this](KScreen::ConfigOperation* op) {
        getConfigFinish = true;

        m_outputsInitTimer->stop();
        if (op->hasError()) {
            USD_LOG(LOG_DEBUG,"Error getting initial configuration：%s",op->errorString().toLatin1().data());
            return;
        }

        if (m_outputsConfig) {
            if (m_outputsConfig->data()) {
                KScreen::ConfigMonitor::instance()->removeConfig(m_outputsConfig->data());
                for (const KScreen::OutputPtr &output : m_outputsConfig->data()->outputs()) {
                    output->disconnect(this);
                }
                m_outputsConfig->data()->disconnect(this);
            }
            m_outputsConfig = nullptr;
        }

        m_outputsConfig = std::unique_ptr<xrandrConfig>(new xrandrConfig(qobject_cast<KScreen::GetConfigOperation*>(op)->config()));
        m_outputsConfig->setValidityFlags(KScreen::Config::ValidityFlag::RequireAtLeastOneEnabledScreen);
        initAllOutputs();
        m_xrandrDbus->mScreenMode = discernScreenMode();
        m_outputsConfig->setScreenMode(m_outputModeEnum.valueToKey(m_xrandrDbus->mScreenMode));
    });
}

XrandrManager::~XrandrManager()
{
    if (m_acitveTimer) {
        delete m_acitveTimer;
        m_acitveTimer = nullptr;
    }
    if (m_xrandrSettings) {
        delete m_xrandrSettings;
        m_xrandrSettings = nullptr;
    }
    qDeleteAll(m_touchMapList);
    m_touchMapList.clear();
}

bool XrandrManager::start()
{
    USD_LOG(LOG_DEBUG,"Xrandr Manager Start");
    connect(m_acitveTimer, &QTimer::timeout, this,&XrandrManager::active);
    m_acitveTimer->start();
    return true;
}

void XrandrManager::stop()
{
     USD_LOG(LOG_DEBUG,"Xrandr Manager Stop");
}

/*查找触摸屏设备ID*/
static bool
find_touchscreen_device(Display* display, XIDeviceInfo *dev)
{
    int i = 0;
    if (dev->use != XISlavePointer) {
        return false;
    }

    if (!dev->enabled) {
        USD_LOG(LOG_DEBUG,"%s device is disabled.",dev->name);
        return false;
    }

    QString devName = QString::fromUtf8(dev->name);
    if (devName.toUpper().contains("TOUCHPAD")) {
        return true;
    }

    for (int j = 0; j < dev->num_classes; j++) {
        if (dev->classes[j]->type == XIValuatorClass) {
            XIValuatorClassInfo *t = (XIValuatorClassInfo*)dev->classes[j];
            // 如果当前的设备是绝对坐标映射 则认为该设备需要进行一次屏幕映射

            if (t->mode == XIModeAbsolute) {
                USD_LOG(LOG_DEBUG,"%s type:%d mode:%d", dev->name, dev->classes[i]->type, t->mode);
                return true;
            }
        }
    }
    return false;
}

/* Get device node for gudev
   node like "/dev/input/event6"
 */
unsigned char *
getDeviceNode (XIDeviceInfo devinfo)
{
    Atom  prop;
    Atom act_type;
    int  act_format;
    unsigned long nitems, bytes_after;
    unsigned char *data;

    prop = XInternAtom(QX11Info::display(), XI_PROP_DEVICE_NODE, False);
    if (!prop) {
        return NULL;
    }

    if (XIGetProperty(QX11Info::display(), devinfo.deviceid, prop, 0, 1000, False,
                      AnyPropertyType, &act_type, &act_format, &nitems, &bytes_after, &data) == Success) {
        return data;//free in calibrateTouchScreen
    }
    return NULL;
}

/* Get touchscreen info */
GList *
getTouchscreen(Display* display)
{
    Q_UNUSED(display);

    gint n_devices;
    XIDeviceInfo *devsInfo;
    GList *tsDevs = NULL;
    Display *dpy = QX11Info::display();
    devsInfo = XIQueryDevice(dpy, XIAllDevices, &n_devices);

    for (int i = 0; i < n_devices; i ++) {
        if (find_touchscreen_device(dpy, &devsInfo[i])) {
            unsigned char *node;
            TsInfo *tsInfo = g_new(TsInfo, 1);
            node = getDeviceNode(devsInfo[i]);

            if (node) {
                tsInfo->input_node = node;
                tsInfo->dev_info = devsInfo[i];
                tsDevs = g_list_append(tsDevs, tsInfo);
            }
        }
    }

    return tsDevs;
}

bool checkMatch(double output_width,  double output_height,
                double input_width, double input_height)
{
    double w_diff, h_diff;

    w_diff = ABS(1 - (output_width / input_width));
    h_diff = ABS(1 - (output_height / input_height));
    USD_LOG(LOG_DEBUG,"w_diff--------%f,h_diff----------%f",w_diff,h_diff);

    if (w_diff < MAX_SIZE_MATCH_DIFF && h_diff < MAX_SIZE_MATCH_DIFF) {
        return true;
    }
    return false;
}

/* Here to run command xinput
   更新触摸屏触点位置
*/

void XrandrManager::calibrateDevice(int input_name, char *output_name , bool isRemapFromFile)
{
    if(!UsdBaseClass::isTablet()) {
        touchpadMap *map = new touchpadMap;
        map->sMonitorName = QString(output_name);
        map->sTouchId = input_name;
        m_touchMapList.append(map);
    }

    char buff[128] = "";
    sprintf(buff, "xinput --map-to-output \"%d\" \"%s\"", input_name, output_name);
    USD_LOG(LOG_DEBUG,"map touch-screen [%s]\n", buff);
    QProcess::execute(buff);
}

bool XrandrManager::getOutputConnected(QString screenName)
{
    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (output->isConnected() && output->name() == screenName ) {
            return true;
        }
    }
    return false;
}

bool XrandrManager::getTouchDeviceCalibrateState(int id)
{
    Q_FOREACH(touchpadMap *map,m_touchMapList) {
        if(map->sTouchId == id) {
            return true;
        }
    }
    return false;
}

bool XrandrManager::getOutputCalibrateState(const QString name)
{
    Q_FOREACH(touchpadMap *map,m_touchMapList) {
        if(map->sMonitorName == name) {
            return true;
        }
    }
    return false;
}
/*
 * 通过ouput的pnpId和monitors.xml中的ventor以及接口类型（VGA,HDMI）进行匹配
 *
*/
bool XrandrManager::readMateToKscreen(char monitorsCount,QMap<QString, QString> &monitorsName)
{
    bool ret = false;
    int xmlErrColumn = 0;
    int xmlErrLine = 0;
    int xmlOutputCount = 0;//xml单个配置组合的屏幕数目
    int matchCount = 0;//硬件接口与ventor匹配的数目。

    QDomNode n;
    QDomElement root;
    QDomDocument doc;

    QString xmlErrMsg;
    QString homePath = getenv("HOME");
    QString monitorFile = homePath+"/.config/monitors.xml";

    OutputsConfig monitorsConfig;

    QFile file(monitorFile);

    if (monitorsCount < 1) {
        USD_LOG(LOG_DEBUG, "skip this function...");
        return false;
    }

    USD_LOG_SHOW_PARAM1(monitorsCount);
    if(!file.open(QIODevice::ReadOnly)) {
        USD_LOG(LOG_ERR,"%s can't be read...",monitorFile.toLatin1().data());
        return false;
    }

    if(!doc.setContent(&file,&xmlErrMsg,&xmlErrLine, &xmlErrColumn)) {
        USD_LOG(LOG_DEBUG,"read %s to doc failed errmsg:%s at %d.%d",monitorFile.toLatin1().data(),xmlErrMsg.toLatin1().data(),xmlErrLine,xmlErrColumn);
        file.close();
        return false;
    }

    m_mateFileTag.clear();
    m_IntDate.clear();

    root=doc.documentElement();
    n=root.firstChild();

    while(!n.isNull()) {
        matchCount = 0;
        xmlOutputCount = 0;

        if (n.isElement()) {
            QDomElement e =n.toElement();
            QDomNodeList list=e.childNodes();

            if (e.isElement() == false) {
               goto NEXT_NODE;//goto next config
            }

            /*a configuration have 4 outputs*/
            for (int i=0;i<list.count();i++) {
                UsdOuputProperty *mateOutput;
                QDomNode node=list.at(i);
                QDomNodeList e2 = node.childNodes();

                if (node.isElement()) {

                    if (node.toElement().tagName() == "clone") {
                        monitorsConfig.m_clone = node.toElement().text();
                        continue;
                    }
                    if (node.toElement().tagName() != "output") {
                        continue;
                    }
                    if (node.toElement().text().isEmpty()) {
                        continue;
                    }
                    if (false == monitorsName.keys().contains(node.toElement().attribute("name"))) {
                        USD_LOG_SHOW_PARAMS(node.toElement().attribute("name").toLatin1().data());
                        continue;
                    }
                    mateOutput = new UsdOuputProperty();
                    mateOutput->setProperty("name", node.toElement().attribute("name"));
                    for (int j=0;j<e2.count();j++) {
                        QDomNode node2 = e2.at(j);
                        mateOutput->setProperty(node2.toElement().tagName().toLatin1().data(),node2.toElement().text());
                    }
                    //多个屏幕组合，需要考虑包含的情况
                    if (monitorsName[mateOutput->property("name").toString()] == mateOutput->property("vendor").toString()) {
                        matchCount++;
                    }
                }
                xmlOutputCount++;
                monitorsConfig.m_outputList.append(mateOutput);
            }

            //monitors.xml内的其中一个configuration屏幕数目与识别数目一致，并且与接口识别出的vendor数目三者一致时才可进行设置。
            if (matchCount != monitorsCount || xmlOutputCount != matchCount) {
                if (monitorsConfig.m_outputList.count()>0) {
                    qDeleteAll(monitorsConfig.m_outputList);
                    monitorsConfig.m_outputList.clear();
                }
               goto NEXT_NODE;//goto next config
            }

            if (monitorsConfig.m_clone == "yes") {
                setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::cloneScreenMode));
                ret = true;
                goto FINISH;
            }

            for (const KScreen::OutputPtr &output: m_configPtr->outputs()) {
                if (false == output->isConnected()) {
                    continue;
                }

                for (int k = 0; k < monitorsConfig.m_outputList.count(); k++) {
                    if (output->name() != monitorsConfig.m_outputList[k]->property("name").toString()) {
                        continue;
                    }

                    bool modeSetOk = false;
                    int x;
                    int y;
                    int width;
                    int height;
                    int rate;
                    int rotationInt;

                    QString primary;
                    QString rotation;

                    width = getMateConfigParam(monitorsConfig.m_outputList[k], "width");
                    if (width < 0) {
                        return false;
                    }

                    height = getMateConfigParam(monitorsConfig.m_outputList[k], "height");
                    if (height < 0) {
                        return false;
                    }

                    x = getMateConfigParam(monitorsConfig.m_outputList[k], "x");
                    if (x < 0) {
                        return false;
                    }

                    y = getMateConfigParam(monitorsConfig.m_outputList[k], "y");
                    if (y < 0) {
                        return false;
                    }

                    rate = getMateConfigParam(monitorsConfig.m_outputList[k], "rate");
                    if (y < 0) {
                        return false;
                    }

                    primary = monitorsConfig.m_outputList[k]->property("primary").toString();
                    rotation = monitorsConfig.m_outputList[k]->property("rotation").toString();

                    if (primary == "yes") {
                        output->setPrimary(true);
                    }
                    else {
                        output->setPrimary(false);
                    }

                    if (rotation == "left") {
                        rotationInt = 2;
                    } else if (rotation == "upside_down") {
                        rotationInt = 4;
                    } else if  (rotation == "right") {
                        rotationInt = 8;
                    } else {
                        rotationInt = 1;
                    }

                    output->setRotation(static_cast<KScreen::Output::Rotation>(rotationInt));

                    Q_FOREACH(auto mode, output->modes()) {
                        if(mode->size().width() != width && mode->size().height() != height) {
                            continue;
                        }
                        if (fabs(mode->refreshRate() - rate) > 2) {
                            continue;
                        }
                        output->setCurrentModeId(mode->id());
                        output->setPos(QPoint(x,y));
                        modeSetOk = true;
                        break;
                    }

                    if (modeSetOk == false) {
                        ret = false;
                        goto FINISH;
                    }
                }
            }
            applyConfig();
            ret = true;
            goto FINISH;
        }
NEXT_NODE:
        n = n.nextSibling();
        qDeleteAll(monitorsConfig.m_outputList);
        monitorsConfig.m_outputList.clear();
    }

FINISH:
    qDeleteAll(monitorsConfig.m_outputList);
    monitorsConfig.m_outputList.clear();
    return ret;
}

int XrandrManager::getMateConfigParam(UsdOuputProperty *mateOutput, QString param)
{
    bool isOk;
    int ret;

    ret = mateOutput->property(param.toLatin1().data()).toInt(&isOk);

    if (false == isOk) {
        return -1;
    }
    return ret;
}

static int findEventFromName(char *_name, char *_serial, char *_event)
{
    int ret = -1;
    if ((NULL == _name) || (NULL == _serial) || (NULL == _event)) {
        USD_LOG(LOG_DEBUG,"parameter NULL ptr.");
        return ret;
    }

    struct udev *udev;
    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_device *dev;

    udev = udev_new();
    enumerate = udev_enumerate_new(udev);

    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *pPath;
        const char *pEvent;
        const char cName[] = "event";
        pPath = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, pPath);
        //touchScreen is usb_device
        dev = udev_device_get_parent_with_subsystem_devtype(
                dev,
                "usb",
                "usb_device");
        if (!dev) {
            continue;
        }

        pEvent = strstr(pPath, cName);
        if(NULL == pEvent) {
            udev_device_unref(dev);
            continue;
        }

        const char *pProduct = udev_device_get_sysattr_value(dev,"product");
        const char *pSerial = udev_device_get_sysattr_value(dev, "serial");

        if(NULL == pProduct) {
            continue;
        }
        //有的设备没有pSerial， 可能导致映射错误， 不处理
        //pProduct 是_name的子串
        if ((NULL == _serial)||(0 == strcmp(MONITOR_NULL_SERIAL, _serial))) {
            if(NULL != strstr(_name, pProduct)) {
                strcpy(_event, pEvent);
                ret = Success;
                udev_device_unref(dev);
                USD_LOG(LOG_DEBUG,"pEvent: %s _name:%s  _serial:%s  product:%s  serial:%s" ,pEvent, _name, _serial, pProduct, pSerial);
                break;
            }
        } else {
            if((NULL != strstr(_name, pProduct)) && (0 == strcmp(_serial, pSerial))) {
                strcpy(_event, pEvent);
                ret = Success;
                udev_device_unref(dev);
                USD_LOG(LOG_DEBUG,"pEvent: %s _name:%s  _serial:%s  product:%s  serial:%s" ,pEvent, _name, _serial, pProduct, pSerial);
                break;
            }
        }
        udev_device_unref(dev);
    }
    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return ret;
}

static int findTouchIdFromEvent(Display *_dpy, char *_event, int *pId)
{
    int ret = -1;
    if((NULL == pId) || (NULL == _event) || (NULL == _dpy)) {
        USD_LOG(LOG_DEBUG,"parameter NULL ptr.");
        return ret;
    }
    int         	i            = 0;
    int             j            = 0;
    int         	numDevices  = 0;
    XDeviceInfo 	*pXDevsInfo = NULL;
    XDevice         *pXDev       = NULL;
    unsigned char 	*cNode       = NULL;
    const char  	cName[]      = "event";
    const char        	*cEvent      = NULL;
    int             nprops       = 0;
    Atom            *props       = NULL;
    char            *name;
    Atom            act_type;
    int             act_format;
    unsigned long   nitems, bytes_after;
    unsigned char   *data;

    pXDevsInfo = XListInputDevices(_dpy, &numDevices);
    for(i = 0; i < numDevices; i++) {
        pXDev = XOpenDevice(_dpy, pXDevsInfo[i].id);
        if (!pXDev) {
            USD_LOG(LOG_DEBUG,"unable to open device '%s'\n", pXDevsInfo[i].name);
            continue;
        }

        props = XListDeviceProperties(_dpy, pXDev, &nprops);
        if (!props) {
            USD_LOG(LOG_DEBUG,"Device '%s' does not report any properties.\n", pXDevsInfo[i].name);
            continue;
        }

        for(j = 0; j < nprops; j++) {
            name = XGetAtomName(_dpy, props[j]);
            if(0 != strcmp(name, "Device Node")) {
                continue;
            }
            XGetDeviceProperty(_dpy, pXDev, props[j], 0, 1000, False,
                                   AnyPropertyType, &act_type, &act_format,
                                   &nitems, &bytes_after, &data);
            cNode = data;
        }

        if(NULL == cNode) {
            continue;
        }

        cEvent = strstr((const char *)cNode, cName);

        if(0 == strcmp(_event, cEvent)) {
            *pId = pXDevsInfo[i].id;
            USD_LOG(LOG_DEBUG,"cNode:%s id:%d ",cNode, *pId);
            ret = Success;
            break;
        }
    }

    return ret;
}

static int findTouchIdFromName(Display *_dpy, char *_name, char *_serial, int *_pId)
{
    int ret = -1;
    if((NULL == _name) || (NULL == _serial) || (NULL == _pId) || (NULL == _dpy)){
        USD_LOG(LOG_DEBUG,"parameter NULL ptr. ");
        goto LEAVE;
    }
    char cEventName[32]; // eg:event25

    ret = findEventFromName(_name, _serial, cEventName);
    if(Success != ret) {
//        USD_LOG(LOG_DEBUG,"find_event_from_name ret[%d]", ret);
        goto LEAVE;
    }

    ret = findTouchIdFromEvent(_dpy, cEventName, _pId);
    if(Success != ret) {
//        USD_LOG(LOG_DEBUG,"find_touchId_from_event ret[%d]", ret);
        goto LEAVE;
    }
LEAVE:
    return ret;
}


int getMapInfoListFromConfig(QString confPath,MapInfoFromFile* mapInfoList)
{
    int ret = -1;
    QSettings *configIniRead = new QSettings(confPath, QSettings::IniFormat);
    int mapNum = configIniRead->value("/COUNT/num").toInt();
    if (mapNum < 1) {
        return ret;
    }
    for (int i = 0; i < mapNum ;++i) {
        QString mapName = QString("/MAP%1/%2");
        QString touchName = configIniRead->value(mapName.arg(i+1).arg("name")).toString();
        QString scrname = configIniRead->value(mapName.arg(i+1).arg("scrname")).toString();
        QString serial = configIniRead->value(mapName.arg(i+1).arg("serial")).toString();
        if(NULL != touchName) {
            mapInfoList[i].sTouchName = touchName;
        }
        if(NULL != scrname) {
            mapInfoList[i].sMonitorName = scrname;
        }
        if(NULL != serial) {
            mapInfoList[i].sTouchSerial = serial;
        }
    }
    return mapNum;
}

/*
 *
 * 触摸设备映射方案：
 * 首先找出输出设备的尺寸，然后找到触摸设备的尺寸，最后如果尺寸一致，则为一一对应的关系，需要处理映射。
 *
 */

void XrandrManager::calibrateTouchScreen()
{
    int     event_base, error_base, major, minor;
    int     o;
    int     xscreen;

    Window  root;
    XRRScreenResources *res;
    Display *dpy = QX11Info::display();
    GList *tsDevs = getTouchscreen (dpy);

    if (!g_list_length (tsDevs)) {
        fprintf(stdin, "No touchscreen find...\n");
        return;
    }

    GList *l = NULL;

    if (!XRRQueryExtension (dpy, &event_base, &error_base) ||
        !XRRQueryVersion (dpy, &major, &minor)) {
        fprintf (stderr, "RandR extension missing\n");
        return;
    }

    xscreen = DefaultScreen (dpy);
    root = RootWindow (dpy, xscreen);

    if ( major >= 1 && minor >= 5) {
        res = XRRGetScreenResources (dpy, root);
        if (!res)
          return;

        for (o = 0; o < res->noutput; o++) {
            bool kscreenHadMap = false;
            XRROutputInfo *output_info = XRRGetOutputInfo (dpy, res, res->outputs[o]);
            if (!output_info){
                fprintf (stderr, "could not get output 0x%lx information\n", res->outputs[o]);
                continue;
            }
            int output_mm_width = output_info->mm_width;
            int output_mm_height = output_info->mm_height;

            if (output_info->connection == 0) {
                if(getOutputCalibrateState(QString(output_info->name))) {
                    continue;
                }
                USD_LOG(LOG_DEBUG,"output_info->name : %s ",output_info->name);
                for ( l = tsDevs; l; l = l->next) {

                    TsInfo *info = (TsInfo *)l -> data;

                    if(getTouchDeviceCalibrateState(info->dev_info.deviceid)) {
                        continue;
                    }
                    gint64 width, height;
                    QString deviceName = QString::fromLocal8Bit(info->dev_info.name);
                    QString ouputName = QString::fromLocal8Bit(output_info->name);
                    const char *udev_subsystems[] = {"input", NULL};

                    GUdevDevice *udev_device;
                    GUdevClient *udev_client = g_udev_client_new (udev_subsystems);
                    udev_device = g_udev_client_query_by_device_file (udev_client,
                                                                      (const gchar *)info->input_node);

                    USD_LOG(LOG_DEBUG,"%s(%d) %d %d had touch",info->dev_info.name,info->dev_info.deviceid,g_udev_device_has_property(udev_device,"ID_INPUT_WIDTH_MM"), g_udev_device_has_property(udev_device,"ID_INPUT_HEIGHT_MM"));
                    //sp1的触摸板不一定有此属性，所以根据名字进行适配by sjh 2021年10月20日11:23:58
                    if ((udev_device && g_udev_device_has_property (udev_device,
                                                                   "ID_INPUT_TOUCHSCREEN")) || g_udev_device_has_property(udev_device,"ID_INPUT_TABLET")) {
                        char *touchDevicePath = NULL;
                        width = g_udev_device_get_property_as_uint64 (udev_device,
                                                                    "ID_INPUT_WIDTH_MM");
                        height = g_udev_device_get_property_as_uint64 (udev_device,
                                                                     "ID_INPUT_HEIGHT_MM");

                        /*对于笔记本而言，i2c接口的触摸类（触摸类设备不一定都是i2c接口，如部分usd接口的touchpad）
                         * 基本属于自带触摸屏，内置触控笔，所以判断outputname是否为eDP-1，接口是否为i2c来判断是否需要映射到内屏。*/

//                        if (UsdBaseClass::isTablet()) //按道理只有笔记本才可以做此操作，但是平板没有盖子无法判断笔记本。
                        {
                            if (g_udev_device_has_property (udev_device,"ID_PATH")) {
                                touchDevicePath = (char *)g_udev_device_get_property(udev_device,"ID_PATH");
                                if (strstr(touchDevicePath,"i2c") && strstr(touchDevicePath,"pci") && ouputName == "eDP-1") {
                                    calibrateDevice(info->dev_info.deviceid,output_info->name);
                                    kscreenHadMap = true;
                                }
                            }
                        }

                        if (checkMatch(output_mm_width, output_mm_height, width, height)) {//
                            if (kscreenHadMap) {
                                continue;
                            }
                            kscreenHadMap = true;
                            calibrateDevice(info->dev_info.deviceid,output_info->name);
                            USD_LOG(LOG_DEBUG,".map checkMatch");
                            if (ouputName != "eDP-1") {
                                break;
                            }
                        }/* else if (deviceName.toUpper().contains("TOUCHPAD") && ouputName == "eDP-1"){//触摸板只映射主屏幕
                            USD_LOG(LOG_DEBUG,".map touchpad.");
//                            doRemapAction(info->dev_info.deviceid,output_info->name);
//                            break;
                            continue;//笔记本可能带有触摸板，触摸屏，触摸笔三个触摸设备映射到一个显示器中
                        }*/
                    }
                    g_clear_object (&udev_client);
                }
                /*屏幕尺寸与触摸设备对应不上且未映射，映射剩下的设备*/
                for ( l = tsDevs; l; l = l->next) {
                    TsInfo *info = (TsInfo *)l -> data;

                    if(getOutputCalibrateState(QString(output_info->name)) || getTouchDeviceCalibrateState(info->dev_info.deviceid)) {
                        continue;
                    }
                    const char *udevSubsystems[] = {"input", NULL};
                    GUdevDevice *udevDevice;
                    GUdevClient *udevClient = g_udev_client_new (udevSubsystems);
                    udevDevice = g_udev_client_query_by_device_file (udevClient,
                                                                      (const gchar *)info->input_node);
                    USD_LOG(LOG_DEBUG,"Size correspondence error");
                    if ((udevDevice && g_udev_device_has_property (udevDevice,
                                                                    "ID_INPUT_TOUCHSCREEN")) || g_udev_device_has_property(udevDevice,"ID_INPUT_TABLET")) {
                        calibrateDevice(info->dev_info.deviceid,output_info->name);
                    }
                    g_clear_object (&udevClient);
                }
            }
        }
    } else {
        USD_LOG(LOG_DEBUG,"xrandr extension too low");
    }

    for (l = tsDevs; l; l = l->next) {
        TsInfo *info = (TsInfo *)l -> data;
        XFree(info->input_node);
    }
    g_list_free(tsDevs);
}


void XrandrManager::calibrateTouchScreenInTablet()
{
    int     event_base, error_base, major, minor;
    int     o;
    Window  root;
    int     xscreen;
    XRRScreenResources *res;
    Display *dpy = QX11Info::display();
    GList *tsDevs = NULL;

    tsDevs = getTouchscreen (dpy);

    if (!g_list_length (tsDevs)) {
        fprintf(stdin, "No touchscreen find...\n");
        return;
    }

    GList *l = NULL;

    if (!XRRQueryExtension (dpy, &event_base, &error_base) ||
        !XRRQueryVersion (dpy, &major, &minor)) {
        fprintf (stderr, "RandR extension missing\n");
        return;
    }

    xscreen = DefaultScreen (dpy);
    root = RootWindow (dpy, xscreen);

    if ( major >= 1 && minor >= 5) {
        res = XRRGetScreenResources(dpy, root);
        if (!res)
          return;

        for (o = 0; o < res->noutput; o++) {
            XRROutputInfo *outputInfo = XRRGetOutputInfo(dpy, res, res->outputs[o]);
            if (!outputInfo) {
                fprintf (stderr, "could not get output 0x%lx information\n", res->outputs[o]);
                continue;
            }
            int output_mm_width = outputInfo->mm_width;
            int output_mm_height = outputInfo->mm_height;

            if (outputInfo->connection == 0) {
                for (l=tsDevs; l; l=l->next) {
                    TsInfo *info = (TsInfo *)l->data;
                    double width, height;
                    const char *udev_subsystems[] = {"input", NULL};

                    GUdevDevice *udevDevice;
                    GUdevClient *udevClient = g_udev_client_new (udev_subsystems);
                    udevDevice = g_udev_client_query_by_device_file (udevClient,
                                                                      (const gchar *)info->input_node);

                    USD_LOG(LOG_DEBUG,"%s(%d) %d %d had touch",info->dev_info.name,info->dev_info.deviceid, g_udev_device_has_property(udevDevice,"ID_INPUT_WIDTH_MM"),
                            g_udev_device_has_property(udevDevice,"ID_INPUT_HEIGHT_MM"));

                    //sp1的触摸板不一定有此属性，所以根据名字进行适配by sjh 2021年10月20日11:23:58
                    if ((udevDevice && (udevDevice && g_udev_device_has_property (udevDevice,
                                                                                    "ID_INPUT_TOUCHSCREEN")) || g_udev_device_has_property(udevDevice,"ID_INPUT_TABLET"))) {
                        width = g_udev_device_get_property_as_double (udevDevice,
                                                                    "ID_INPUT_WIDTH_MM");
                        height = g_udev_device_get_property_as_double (udevDevice,
                                                                     "ID_INPUT_HEIGHT_MM");

                        if (checkMatch(output_mm_width, output_mm_height, width, height)) {//
                            USD_LOG(LOG_DEBUG,".output_mm_width:%d  output_mm_height:%d  width:%d. height:%d",output_mm_width,output_mm_height,width,height);
                            calibrateDevice(info->dev_info.deviceid,outputInfo->name);
                        }/* else if (deviceName.toUpper().contains("TOUCHPAD") && ouputName == "eDP-1"){//触摸板只映射主屏幕
                            USD_LOG(LOG_DEBUG,".map touchpad.");
                            doRemapAction(info->dev_info.deviceid,output_info->name);
                        }*/
                    }
                    g_clear_object (&udevClient);
                }
            }
        }
    } else {
        USD_LOG(LOG_DEBUG,"xrandr extension too low");
    }

    for (l=tsDevs; l; l=l->next) {
        TsInfo *info = (TsInfo *)l->data;
        XFree(info->input_node);
    }
    g_list_free(tsDevs);
}

void XrandrManager::calibrateTouchDeviceWithConfigFile(QString mapPath)
{
    MapInfoFromFile mapInfoList[64];
    Display *pDpy = XOpenDisplay(NULL);
    int deviceId = 0;
    int mapNum = getMapInfoListFromConfig(mapPath,mapInfoList);
    USD_LOG(LOG_DEBUG,"getMapInfoListFromConfig : %d",mapNum);
    for (int i = 0; i<mapNum; ++i) {
        int ret = findTouchIdFromName(pDpy, mapInfoList[i].sTouchName.toLatin1().data(),mapInfoList[i].sTouchSerial.toLatin1().data(), &deviceId);
        USD_LOG(LOG_DEBUG,"find_touchId_from_name : %d",deviceId);
        if(Success == ret){
            //屏幕连接时进行映射
            if(getOutputConnected(mapInfoList[i].sMonitorName)) {
                calibrateDevice(deviceId,mapInfoList[i].sMonitorName.toLatin1().data(),true);
            }
        }
    }
    XCloseDisplay(pDpy);
}

/*监听旋转键值回调 并设置旋转角度*/
void XrandrManager::doRotationChanged(const QString &rotation)
{
    int value = 0;
    QString angle_Value = rotation;

    if (angle_Value == "normal") {
        value = 1;
    } else if (angle_Value == "left") {
        value = 2;
    } else if (angle_Value == "upside-down") {
        value = 4;
    } else if  (angle_Value == "right") {
        value = 8;
    } else {
        USD_LOG(LOG_ERR,"Find a error !!!");
        return ;
    }

    const KScreen::OutputList outputs = m_outputsConfig->data()->outputs();
    for(auto output : outputs){
        if (!output->isConnected() || !output->isEnabled() || !output->currentMode()) {
            continue;
        }
        output->setRotation(static_cast<KScreen::Output::Rotation>(value));
        USD_LOG(LOG_DEBUG,"set %s rotaion:%s", output->name().toLatin1().data(), rotation.toLatin1().data());
    }
    applyConfig();
}


void XrandrManager::doOutputsConfigurationChanged()
{
    USD_LOG(LOG_DEBUG,"...");
}

void XrandrManager::calibrateTouchDevice()
{
    static KScreen::ConfigPtr oldOutputs = nullptr;
    bool needSetMapTouchDevice = false;

    if (oldOutputs != nullptr) {
        Q_FOREACH(const KScreen::OutputPtr &oldOutput, oldOutputs->outputs()) {
            if (needSetMapTouchDevice) {
                break;
            }
            Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
                if(output->isConnected() && oldOutput->name() == output->name()) {
                    if(oldOutput->currentModeId() != output->currentModeId() || oldOutput->pos() != output->pos()
                            || oldOutput->scale() != output->scale() || oldOutput->rotation() != output->rotation() ||
                            oldOutput->isPrimary() != output->isPrimary() || oldOutput->isEnabled() != output->isEnabled() ||
                            oldOutput->isConnected() != output->isConnected()) {
                        needSetMapTouchDevice = true;
                    }
                    if(oldOutput->currentMode().isNull() && output->currentMode().isNull()) {
                        continue;
                    }
                    if(oldOutput->currentMode().isNull() || output->currentMode().isNull()) {
                        needSetMapTouchDevice = true;
                        break;
                    } else {
                        if(oldOutput->currentMode()->size() != output->currentMode()->size()) {
                            needSetMapTouchDevice = true;
                        }
                    }
                }
                if(oldOutput->currentMode().isNull() && output->currentMode().isNull()) {
                    break;
                }
                if(oldOutput->currentMode().isNull() || output->currentMode().isNull()) {
                    needSetMapTouchDevice = true;
                    break;
                } else {
                    if(oldOutput->currentMode()->size() != output->currentMode()->size()) {
                        needSetMapTouchDevice = true;
                    }
                }
            }
        }
    } else {
        needSetMapTouchDevice = true;
    }
    oldOutputs = m_outputsConfig->data()->clone();
    if (needSetMapTouchDevice) {
        if (UsdBaseClass::isTablet()) {
            calibrateTouchScreenInTablet();
        } else {
            qDeleteAll(m_touchMapList);
            m_touchMapList.clear();
            QString configPath = QDir::homePath() +  MAP_CONFIG;
            QFileInfo file(configPath);
            if(file.exists()) {
                calibrateTouchDeviceWithConfigFile(configPath);
            }
            calibrateTouchScreen();
        }
    }
}


void XrandrManager::sendOutputsModeToDbus()
{
    const QStringList ukccModeList = {"first", "copy", "expand", "second"};
    int screenConnectedCount = 0;
    int screenMode = discernScreenMode();

    m_xrandrDbus->sendModeChangeSignal(screenMode);
    m_xrandrDbus->sendScreensParamChangeSignal(m_outputsConfig->getScreensParam());
    ///send screens mode to ukcc(ukui-control-center) by sjh 2021.11.08
    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (true == output->isConnected()) {
            screenConnectedCount++;
        }
    }

    if (screenConnectedCount > 1) {
        m_ukccDbus->call("setScreenMode", ukccModeList[screenMode]);
    } else {
        m_ukccDbus->call("setScreenMode", ukccModeList[0]);
    }
}

void XrandrManager::applyConfig()
{
    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        USD_LOG_SHOW_OUTPUT(output);
    }
    if (m_outputsConfig->canBeApplied()) {
        m_isSetting = true;
        connect(new KScreen::SetConfigOperation(m_outputsConfig->data()),
                &KScreen::SetConfigOperation::finished,
                this, [this]() {
            QProcess subProcess;
            QString usdSaveParam = "save-param -g";

            USD_LOG(LOG_ERR,"--|apply success|--");
            calibrateTouchDevice();
            sendOutputsModeToDbus();

            m_outputsConfig->setScreenMode(m_outputModeEnum.valueToKey(discernScreenMode()));
            m_outputsConfig->writeFile(false);

            USD_LOG(LOG_DEBUG,"save param in lightdm-data.");
            subProcess.start(usdSaveParam);
            subProcess.waitForFinished();
            m_isSetting = false;
        });
    } else {
        USD_LOG(LOG_ERR,"--|can't be apply|--");
        m_isSetting = false;
        Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
            USD_LOG_SHOW_OUTPUT(output);
        }
    }
}

//用于外置显卡，当外置显卡插拔时会有此事件产生
void XrandrManager::doOutputAdded(const KScreen::OutputPtr &output)
{
    USD_LOG_SHOW_OUTPUT(output);
    if (!m_outputsConfig->data()->outputs().contains(output->id())) {
        m_outputsConfig->data()->addOutput(output->clone());
        connect(output.data(), &KScreen::Output::isConnectedChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }

            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
            if (senderOutput->isConnected()==false) {
                USD_LOG(LOG_DEBUG,"ready remove %d",senderOutput->id());
                if (m_outputsConfig->data()->outputs().contains(senderOutput->id())) {
                    USD_LOG(LOG_DEBUG,"remove %d",senderOutput->id());
                    m_outputsConfig->data()->removeOutput(senderOutput->id());
                }
            }
            m_offUsbScreenTimer->start(1500);
            Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
                USD_LOG_SHOW_OUTPUT(output);
            }

        },Qt::QueuedConnection);
    }
    m_onUsbScreenTimer->start(2500);
}


void XrandrManager::doOutputRemoved(int outputId)
{
     if (!m_outputsConfig->data()->outputs().contains(outputId)) {
        return;
     }

     m_outputsConfig->data()->removeOutput(outputId);
     std::unique_ptr<xrandrConfig> MonitoredConfig = m_outputsConfig->readFile(false);
     if (MonitoredConfig == nullptr ) {
         USD_LOG(LOG_DEBUG,"config a error");
         setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::cloneScreenMode));
         return;
     }
     m_outputsConfig = std::move(MonitoredConfig);
     applyConfig();
}

void XrandrManager::doPrimaryOutputChanged(const KScreen::OutputPtr &output)
{
    USD_LOG(LOG_DEBUG,".");
}


/*
 *
 *接入时没有配置文件的处理流程：
 *单屏：最优分辨率。
 *多屏幕：镜像模式。
 *
*/
void XrandrManager::outputConnectedWithoutConfigFile(KScreen::Output *newOutput, char outputCount)
{
    if (1 == outputCount) {//单屏接入时需要设置模式，主屏
        setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::firstScreenMode));
    } else {
        setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::cloneScreenMode));
    }

}

void XrandrManager::lightLastScreen()
{
    int enableCount = 0;
    int connectCount = 0;

    Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
        if (output->isConnected()){
            connectCount++;
        }
        if (output->isEnabled()){
            enableCount++;
        }
    }
    if (connectCount==1 && enableCount==0){
        Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
            if (output->isConnected()){
                output->setEnabled(true);
                break;
            }
        }
    }
}

int XrandrManager::getCurrentRotation()
{
    uint8_t ret = 1;
    QDBusMessage message = QDBusMessage::createMethodCall(DBUS_STATUSMANAGER_NAME,
                                                          DBUS_STATUSMANAGER_PATH,
                                                          DBUS_STATUSMANAGER_NAME,
                                                          DBUS_STATUSMANAGER_GET_ROTATION);

    QDBusMessage response = QDBusConnection::sessionBus().call(message);
    if (response.type() == QDBusMessage::ReplyMessage) {
        if (response.arguments().isEmpty() == false) {
            QString value = response.arguments().takeFirst().toString();
            USD_LOG(LOG_DEBUG, "get mode :%s", value.toLatin1().data());
            if (value == "normal") {
                ret = 1;
            } else if (value == "left") {
                 ret = 2;
            } else if (value == "upside-down") {
                  ret = 4;
            } else if  (value == "right") {
                   ret = 8;
            } else {
                USD_LOG(LOG_DEBUG,"Find a error !!! value%s",value.toLatin1().data());
                return ret = 1;
            }
        }
    }
    return ret;
}

/*
 *
 * -1:无接口
 * 0:PC模式
 * 1：tablet模式
 *
*/
int XrandrManager::getCurrentMode()
{
    QDBusMessage message = QDBusMessage::createMethodCall(DBUS_STATUSMANAGER_NAME,
                                                          DBUS_STATUSMANAGER_PATH,
                                                          DBUS_STATUSMANAGER_NAME,
                                                          DBUS_STATUSMANAGER_GET_MODE);

    QDBusMessage response = QDBusConnection::sessionBus().call(message);
    if (response.type() == QDBusMessage::ReplyMessage) {
        if(response.arguments().isEmpty() == false) {
            bool value = response.arguments().takeFirst().toBool();
            return value;
        }
    }
    return -1;
}

void XrandrManager::doOutputChanged(KScreen::Output *senderOutput)
{
    char outputConnectCount = 0;
    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (output->name()==senderOutput->name()) {
            senderOutput->setEnabled(senderOutput->isConnected());
            m_outputsConfig->data()->removeOutput(output->id());
            m_outputsConfig->data()->addOutput(senderOutput->clone());
            break;
        }
    }
    Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
        if (output->name()==senderOutput->name()) {//这里只设置connect，enbale由配置设置。
            output->setEnabled(senderOutput->isConnected());
            output->setConnected(senderOutput->isConnected());
            output->setModes(senderOutput->modes());
       }
        if (output->isConnected()) {
            outputConnectCount++;
        }
    }
    if (UsdBaseClass::isTablet()) {
        int ret = getCurrentMode();
        USD_LOG(LOG_DEBUG,"intel edu is't need use config file");
        if (0 < ret) {
            //tablet模式
              setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::cloneScreenMode));
        } else {
            //PC模式
              setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::extendScreenMode));
        }
    } else {//非tablet项目无此接口
        if (false == m_outputsConfig->fileExists()) {
            if (senderOutput->isConnected()) {
                senderOutput->setEnabled(senderOutput->isConnected());
            }
            outputConnectedWithoutConfigFile(senderOutput, outputConnectCount);
        } else {
            if (outputConnectCount) {
                std::unique_ptr<xrandrConfig> MonitoredConfig  = m_outputsConfig->readFile(false);
                if (MonitoredConfig!=nullptr) {
                    m_outputsConfig = std::move(MonitoredConfig);
                } else {
                    if (outputConnectCount>1) {
                        if (!checkSettable(UsdBaseClass::eScreenMode::cloneScreenMode)) {
                            if (!checkSettable(UsdBaseClass::eScreenMode::extendScreenMode)) {
                                return setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::cloneScreenMode));
                            }
                        }
                    }
                    USD_LOG(LOG_DEBUG,"read config file error! ");
                }
            }
        }
    }

    USD_LOG(LOG_DEBUG,"read config file success!");

    applyConfig();
    if (UsdBaseClass::isJJW7200()) {
        QTimer::singleShot(3*1000, this, [this](){
            applyConfig();
            USD_LOG(LOG_DEBUG,"signalShot......");
        });
    }
}

void XrandrManager::doOutputModesChanged()
{
//TODO: just handle modesChanges signal for cloud desktop
/*
 * make sure the size in Kscreen config is smaller than max screen size
 * if ouputname != nullptr set this ouput mode is preffer mode,
*/
/*
 * 确保kscreen中的size小于screen的最大尺寸，
 * 如果ouputname不为空，调整output的最佳分辨率，重新进行设置，并计算匹配size，
 * 如果不符合标准则调整另一个屏幕的大小。
 * 如果output为空，则屏幕均使用最佳分辨率，如果无最佳分辨率就用最大最适合。如果第三个屏幕无法接入，则不进行处理。。
 * 目前只按照两个屏幕进行处理。
 * 有时模式改变会一次性给出两个屏幕的信号，就需要在这里同时处理多块屏幕!。
 * 第一步：找到坐标为0，0的屏幕。
 * 第二步：横排所有屏幕
*/
    int newPosX = 0;
    int findOutputCounect = 0;
    //找出pos(0.0)的屏幕大小
    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (!output->isConnected() || !output->isEnabled()) {
            continue;
        }
        //不能在这里获取size大小，特殊情况下currentMode->size会报错
        if (output->pos() == QPoint(0,0)) {
            findOutputCounect++;
            if (m_modesChangeOutputs.contains(output->name()) &&
                    output->modes().contains(output->preferredModeId())) {
                output->setCurrentModeId(output->preferredModeId());
            }
            newPosX += output->currentMode()->size().width();
            break;
        }
    }

    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (!output->isConnected() || !output->isEnabled()) {
            continue;
        }
        if (output->pos() != QPoint(0,0)) {
            output->setPos(QPoint(newPosX,0));
            if (m_modesChangeOutputs.contains(output->name()) &&
                    output->modes().contains(output->preferredModeId())) {
                 output->setCurrentModeId(output->preferredModeId());
            }
            newPosX += output->currentMode()->size().width();
        }
    }
    applyConfig();
}

//处理来自控制面板的操作,保存配置
void XrandrManager::doSaveConfigTimeOut()
{
    int enableScreenCount = 0;
    m_screenSignalTimer->stop();

    if (m_outputsChangedSignal & eScreenSignal::isModesChanged && !(m_outputsChangedSignal & eScreenSignal::isConnectedChanged)) {
        USD_LOG(LOG_DEBUG,".");
        doOutputModesChanged();
        m_modesChangeOutputs.clear();
        m_outputsChangedSignal = eScreenSignal::isNone;
        return;
    }

    if (m_outputsChangedSignal&(eScreenSignal::isConnectedChanged|eScreenSignal::isOutputChanged)) {
        USD_LOG(LOG_DEBUG,"skip save config");
        m_applyConfigWhenSave = false;
        m_outputsChangedSignal = eScreenSignal::isNone;
        return;
    }

    m_outputsChangedSignal = eScreenSignal::isNone;
    if (false == m_applyConfigWhenSave) {
        Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
            if (output->isEnabled()) {
                enableScreenCount++;
            }
        }

        if (0 == enableScreenCount) {//When user disable last one connected screen USD must enable the screen.
            m_applyConfigWhenSave = true;
            m_screenSignalTimer->start(SAVE_CONFIG_TIME*5);
            return;
        }
    }

    if (m_applyConfigWhenSave) {
        m_applyConfigWhenSave = false;
        setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::firstScreenMode));
    } else {
        QProcess subProcess;
        Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
            USD_LOG_SHOW_OUTPUT(output);
        }
        m_outputsConfig->setScreenMode(m_outputModeEnum.valueToKey(discernScreenMode()));
        m_outputsConfig->writeFile(false);
        QString usdSaveParam = "save-param -g";
        USD_LOG(LOG_DEBUG,"save param in lightdm-data.");
        subProcess.start(usdSaveParam);
        subProcess.waitForFinished();
//        SetTouchscreenCursorRotation();//When other app chenge screen'param usd must remap touch device
        calibrateTouchDevice();
        sendOutputsModeToDbus();
    }
}

QString XrandrManager::getOutputsInfo()
{
    return m_outputsConfig->getScreensParam();
}

void XrandrManager::initAllOutputs()
{
    char connectedOutputCount = 0;
    QMap<QString, QString> outputsList;
    if (m_configPtr) {
        KScreen::ConfigMonitor::instance()->removeConfig(m_configPtr);
        for (const KScreen::OutputPtr &output : m_configPtr->outputs()) {
            output->disconnect(this);
        }
        m_configPtr->disconnect(this);
    }

    m_configPtr = std::move(m_outputsConfig->data());

    for (const KScreen::OutputPtr &output: m_configPtr->outputs()) {
        if (output->isConnected()){
            connectedOutputCount++;
            outputsList.insert(output->name(),output->edid()->pnpId());
        }
        connect(output.data(), &KScreen::Output::isConnectedChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }
            m_outputsChangedSignal |= eScreenSignal::isConnectedChanged;
            USD_LOG(LOG_DEBUG,"%s isConnectedChanged connect:%d",senderOutput->name().toLatin1().data(), senderOutput->isConnected());
            doOutputChanged(senderOutput);
        },Qt::QueuedConnection);

        connect(output.data(), &KScreen::Output::outputChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }
            m_outputsChangedSignal |= eScreenSignal::isOutputChanged;
            USD_LOG(LOG_DEBUG,"%s outputchanged connect:%d",senderOutput->name().toLatin1().data(), senderOutput->isConnected());
            m_screenSignalTimer->stop();
            if (UsdBaseClass::isJJW7200()){
                USD_LOG(LOG_DEBUG,"catch a jjw7200..");
                doOutputChanged(senderOutput);
            }
        },Qt::QueuedConnection);

        connect(output.data(), &KScreen::Output::isPrimaryChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }
            m_outputsChangedSignal |= eScreenSignal::isPrimaryChanged;
            USD_LOG(LOG_DEBUG,"PrimaryChanged:%s",senderOutput->name().toLatin1().data());

            Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
                if (output->name() == senderOutput->name()) {
                    output->setPrimary(senderOutput->isPrimary());
                    break;
                }
            }
            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
        });

        connect(output.data(), &KScreen::Output::posChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }

            if (m_outputsChangedSignal & (eScreenSignal::isConnectedChanged|eScreenSignal::isOutputChanged)) {
                return;
            }

            m_outputsChangedSignal |= eScreenSignal::isPosChanged;
            USD_LOG(LOG_DEBUG,"posChanged:%s",senderOutput->name().toLatin1().data());
            Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
                if (output->name() == senderOutput->name()) {
                    output->setPos(senderOutput->pos());
                    break;
                }
            }
            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
        },Qt::QueuedConnection);

        connect(output.data(), &KScreen::Output::sizeChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }
            m_outputsChangedSignal |= eScreenSignal::isSizeChanged;
            USD_LOG(LOG_DEBUG,"sizeChanged:%s",senderOutput->name().toLatin1().data());
            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
        },Qt::QueuedConnection);

        connect(output.data(), &KScreen::Output::modesChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }

            if (!(m_outputsChangedSignal & eScreenSignal::isConnectedChanged)) {
                Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
                    if (output->name()==senderOutput->name()) {//这里只设置connect，enbale由配置设置。
                        if (output->preferredModeId() == nullptr) {
                             USD_LOG(LOG_DEBUG,"%s prefferedMode is none", senderOutput->name());
                             return;
                        }
                        output->setEnabled(senderOutput->isConnected());
                        output->setConnected(senderOutput->isConnected());
                        output->setModes(senderOutput->modes());
                        USD_LOG(LOG_DEBUG,"old mode id:%s", output->preferredModeId().toLatin1().data());
                        output->setPreferredModes(senderOutput->preferredModes());
                        USD_LOG(LOG_DEBUG,"new mode id:%s", output->preferredModeId().toLatin1().data());
                        break;
                    }
                }
                m_modesChangeOutputs.append(senderOutput->name());
                m_outputsChangedSignal |= eScreenSignal::isModesChanged;
            }
            USD_LOG(LOG_DEBUG,"%s modesChanged",senderOutput->name().toLatin1().data());
            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
        },Qt::QueuedConnection);

        connect(output.data(), &KScreen::Output::clonesChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }
            m_outputsChangedSignal |= eScreenSignal::isClonesChanged;
            USD_LOG(LOG_DEBUG,"clonesChanged:%s",senderOutput->name().toLatin1().data());
            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
        },Qt::QueuedConnection);

        connect(output.data(), &KScreen::Output::rotationChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }
            m_outputsChangedSignal |= eScreenSignal::isRotationChanged;
            USD_LOG(LOG_DEBUG,"rotationChanged:%s",senderOutput->name().toLatin1().data());
            Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
                if (output->name() == senderOutput->name()) {
                    output->setRotation(senderOutput->rotation());
                    break;
                }
            }

            USD_LOG(LOG_DEBUG,"rotationChanged:%s",senderOutput->name().toLatin1().data());
            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
        },Qt::QueuedConnection);

        connect(output.data(), &KScreen::Output::currentModeIdChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }
            USD_LOG(LOG_DEBUG,"currentModeIdChanged:%s",senderOutput->name().toLatin1().data());
            m_outputsChangedSignal |= eScreenSignal::isCurrentModeIdChanged;
            Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
                if (output->name() == senderOutput->name()) {
                    output->setCurrentModeId(senderOutput->currentModeId());
                    output->setEnabled(senderOutput->isEnabled());
                    break;
                }
            }

            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
        },Qt::QueuedConnection);

        connect(output.data(), &KScreen::Output::isEnabledChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }

            if (m_isSetting) {
                USD_LOG(LOG_ERR,"skip enable Changed signal until applyConfig over");
                return;
            }

            USD_LOG(LOG_DEBUG,"%s isEnabledChanged %d ",senderOutput->name().toLatin1().data(),senderOutput->isEnabled());
            m_outputsChangedSignal |= eScreenSignal::isEnabledChanged;

            Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
                if (output->name() == senderOutput->name()) {
                    if (output->isConnected() == senderOutput->isConnected()) {
                        output->setEnabled(senderOutput->isEnabled());
                        break;
                    }
                }
            }

            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
        },Qt::QueuedConnection);

        connect(output.data(), &KScreen::Output::scaleChanged, this, [this](){
            KScreen::Output *senderOutput = static_cast<KScreen::Output*> (sender());
            if (senderOutput == nullptr) {
                USD_LOG(LOG_DEBUG,"had a bug..");
                return;
            }
            USD_LOG(LOG_DEBUG,"%s scaleChanged",senderOutput->name().toLatin1().data());
            m_outputsChangedSignal |= eScreenSignal::isEnabledChanged;
            Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
                if (output->name() == senderOutput->name()) {
                    output->setScale(senderOutput->scale());
                    break;
                }
            }

            m_screenSignalTimer->start(SAVE_CONFIG_TIME);
        },Qt::QueuedConnection);
    }

    KScreen::ConfigMonitor::instance()->addConfig(m_configPtr);
    //connect(mConfig.data(), &KScreen::Config::outputAdded,
    //        this, &XrandrManager::outputAddedHandle);

    connect(m_configPtr.data(), SIGNAL(outputAdded(KScreen::OutputPtr)),
            this, SLOT(doOutputAdded(KScreen::OutputPtr)));

    connect(m_configPtr.data(), SIGNAL(outputRemoved(int)),
            this, SLOT(doOutputRemoved(int)));
    
    connect(m_configPtr.data(), &KScreen::Config::primaryOutputChanged,
            this, &XrandrManager::doPrimaryOutputChanged);

    connect(KScreen::ConfigMonitor::instance(), &KScreen::ConfigMonitor::configurationChanged,
            this, &XrandrManager::doOutputsConfigurationChanged, Qt::UniqueConnection);


    if (m_xrandrSettings->keys().contains("hadmate2kscreen")) {//兼容mate配置
        if (m_xrandrSettings->get("hadmate2kscreen").toBool() == false) {
            m_xrandrSettings->set("hadmate2kscreen", true);
            if (readMateToKscreen(connectedOutputCount, outputsList)) {
                USD_LOG(LOG_DEBUG,"convert mate ok...");
                return;
            }
        }
    }

    if(m_outputsConfig->scaleFileExists()) {
       bool x =  m_outputsConfig->mvScaleFile();
    } else {
        if (!m_outputsConfig->fileExists() && connectedOutputCount > 0) {
            m_outputsConfig->writeFile(false);
        }
    }

    if (m_outputsConfig->fileExists()) {
        USD_LOG(LOG_DEBUG,"read config:%s.",m_outputsConfig->filePath().toLatin1().data());

        if (UsdBaseClass::isTablet()) {
            for (const KScreen::OutputPtr &output: m_configPtr->outputs()) {
                if (output->isConnected() && output->isEnabled()) {
                    output->setRotation(static_cast<KScreen::Output::Rotation>(getCurrentRotation()));
                }
            }
        } else {
            int needSetParamWhenStartup = false;
            std::unique_ptr<xrandrConfig> monitoredConfig = m_outputsConfig->readFile(false);

            if (monitoredConfig == nullptr ) {
                USD_LOG(LOG_DEBUG,"config a error");
                setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::cloneScreenMode));
                return;
            }

            m_outputsConfig = std::move(monitoredConfig);
            Q_FOREACH(const KScreen::OutputPtr &oldOutput, m_configPtr->outputs()) {
                if (!oldOutput->isConnected()) {
                    continue;
                }

                if (needSetParamWhenStartup) {
                    break;
                }

                Q_FOREACH(const KScreen::OutputPtr &output,m_outputsConfig->data()->outputs()) {
                    if(oldOutput->name() == output->name()) {
                        USD_LOG_SHOW_PARAMS(oldOutput->name().toLatin1().data());
                        USD_LOG_SHOW_OUTPUT(output);
                        USD_LOG_SHOW_OUTPUT(oldOutput);
                        if(oldOutput->size() != output->size() || oldOutput->pos() != output->pos()
                                || oldOutput->scale() != output->scale() || oldOutput->rotation() != output->rotation() ||
                                oldOutput->isPrimary() != output->isPrimary() || oldOutput->isEnabled() != output->isEnabled()) {
                            needSetParamWhenStartup = true;
                            break;
                        }
                        if(oldOutput->currentMode().isNull() && output->currentMode().isNull()) {
                            break;
                        }
                        if(oldOutput->currentMode().isNull() || output->currentMode().isNull()) {
                            needSetParamWhenStartup = true;
                            break;
                        } else {
                            if(oldOutput->currentMode()->size() != output->currentMode()->size()) {
                                needSetParamWhenStartup = true;
                            }
                        }
                    }
                }
            }

            if (needSetParamWhenStartup) {
                applyConfig();
            }
        }
    }
}

bool XrandrManager::checkPrimaryOutputsIsSetable()
{
    int connecedScreenCount = 0;
    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()){
        if (output->isConnected()){
            connecedScreenCount++;
        }
    }

    if (connecedScreenCount < 2) {
        USD_LOG(LOG_DEBUG, "skip set command cuz ouputs count :%d connected:%d",m_outputsConfig->data()->outputs().count(), connecedScreenCount);
        return false;
    }

    if (nullptr == m_outputsConfig->data()->primaryOutput()){
        USD_LOG(LOG_DEBUG,"can't find primary screen.");
        Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
            if (output->isConnected()) {
                output->setPrimary(true);
                output->setEnabled(true);
                USD_LOG(LOG_DEBUG,"set %s as primary screen.",output->name().toLatin1().data());
                break;
            }
        }
    }
    return true;
}

bool XrandrManager::readAndApplyOutputsModeFromConfig(UsdBaseClass::eScreenMode eMode)
{
     if (UsdBaseClass::isTablet()) {
         return false;
     }

    m_outputsConfig->setScreenMode(m_outputModeEnum.valueToKey(eMode));
    if (m_outputsConfig->fileScreenModeExists(m_outputModeEnum.valueToKey(eMode))) {
        std::unique_ptr<xrandrConfig> MonitoredConfig = m_outputsConfig->readFile(true);
        if (MonitoredConfig == nullptr) {
            USD_LOG(LOG_DEBUG,"config a error");
            return false;
        } else {
            m_outputsConfig = std::move(MonitoredConfig);
            if (checkSettable(eMode)) {
                applyConfig();
                return true;
            }
        }
    }
    return false;
}

bool XrandrManager::checkSettable(UsdBaseClass::eScreenMode eMode)
{
    QList<QRect> listQRect;
    int x=0;
    int y=0;
    bool isSameRect = true;

    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()){
        if (!output->isConnected() || !output->isEnabled()){
            continue;
        }
        listQRect<<output->geometry();
        x += output->geometry().x();
        y += output->geometry().y();
    }

    for (int i = 0; i < listQRect.size()-1; i++){
        if (listQRect.at(i) != listQRect.at(i+1)) {
            isSameRect = false;
        }
    }

    if (eMode == UsdBaseClass::eScreenMode::cloneScreenMode) {
        if (!isSameRect) {
            return false;
        }
    } else if (eMode == UsdBaseClass::eScreenMode::extendScreenMode) {
        if (isSameRect || (x==y && x==0)) {
            return false;
        }
    }
    return true;
}

void XrandrManager::doTabletModeChanged(const bool tablemode)
{
    int screenConnectedCount = 0;
    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (true == output->isConnected()) {
            screenConnectedCount++;
        }
    }

    if (screenConnectedCount<2) {
        return;
    }
    if(tablemode) {
        setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::cloneScreenMode));
    } else {
        setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::extendScreenMode));
    }
    USD_LOG(LOG_DEBUG,"recv mode changed:%d", tablemode);
}

void XrandrManager::setOutputsModeToClone()
{
    int bigestResolution = 0;
    bool hadFindFirstScreen = false;

    QString primaryModeId;
    QString secondaryModeId;
    QString secondScreen;

    QSize primarySize(0,0);
    float primaryRefreshRate = 0;
    float secondaryRefreshRate = 0;

    KScreen::OutputPtr primaryOutput;// = mMonitoredConfig->data()->primaryOutput();

    if (false == checkPrimaryOutputsIsSetable()) {
        return;
    }
    if (readAndApplyOutputsModeFromConfig(UsdBaseClass::eScreenMode::cloneScreenMode)) {
        return;
    }
    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {

        if (false == output->isConnected()) {
            continue;
        }

        output->setEnabled(true);
        output->setPos(QPoint(0,0));
        output->setRotation(static_cast<KScreen::Output::Rotation>(1));
        if (false == hadFindFirstScreen) {
            hadFindFirstScreen = true;
            primaryOutput = output;
            continue;
        }
        output->setPos(QPoint(0,0));
        secondScreen = output->name().toLatin1().data();
        //遍历模式找出最大分辨率的克隆模式
        Q_FOREACH(auto primaryMode, primaryOutput->modes()) {
            Q_FOREACH(auto newOutputMode, output->modes()) {
                primaryOutput->setPos(QPoint(0,0));
                bigestResolution = primarySize.width()*primarySize.height();

                if (primaryMode->size() == newOutputMode->size()) {
                    if (bigestResolution < primaryMode->size().width() * primaryMode->size().height()) {
                        primarySize = primaryMode->size();
                        primaryRefreshRate = primaryMode->refreshRate();
                        primaryOutput->setCurrentModeId(primaryMode->id());
                        secondaryRefreshRate = newOutputMode->refreshRate();
                        output->setCurrentModeId(newOutputMode->id());
                    } else if (bigestResolution ==  primaryMode->size().width() * primaryMode->size().height()) {
                        if (primaryRefreshRate < primaryMode->refreshRate()) {
                            primaryRefreshRate = primaryMode->refreshRate();
                             primaryOutput->setCurrentModeId(primaryMode->id());
                        }
                        if (secondaryRefreshRate < newOutputMode->refreshRate()) {
                            secondaryRefreshRate = newOutputMode->refreshRate();
                            output->setCurrentModeId(newOutputMode->id());
                        }
                    }
                }
            }
        }

        if (UsdBaseClass::isTablet()) {
            output->setRotation(static_cast<KScreen::Output::Rotation>(getCurrentRotation()));
            primaryOutput->setRotation(static_cast<KScreen::Output::Rotation>(getCurrentRotation()));
        }
        USD_LOG_SHOW_OUTPUT(output);
    }

    if (0 == bigestResolution) {
       setOutputsMode(m_outputModeEnum.key(UsdBaseClass::eScreenMode::extendScreenMode));
    } else {
       applyConfig();
    }
}

void XrandrManager::setOutputsModeToFirst(bool isFirstMode)
{
    int posX = 0;
    int maxScreenSize = 0;
    bool hadFindFirstScreen = false;
    bool hadSetPrimary = false;
    float refreshRate = 0.0;

    if (false == checkPrimaryOutputsIsSetable()) {
        //return; //因为有用户需要在只有一个屏幕的情况下进行了打开，所以必须走如下流程。
    }
    if (isFirstMode){
        if (readAndApplyOutputsModeFromConfig(UsdBaseClass::eScreenMode::firstScreenMode)) {
            return;
        }
    } else {
        if (readAndApplyOutputsModeFromConfig(UsdBaseClass::eScreenMode::secondScreenMode)) {
            return;
        }
    }

    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (output->isConnected()) {
            output->setEnabled(true);
        } else {
            output->setEnabled(false);
            continue;
        }
        //找到第一个屏幕（默认为内屏）
        if (hadFindFirstScreen) {
            output->setEnabled(!isFirstMode);
        } else {
            hadFindFirstScreen = true;
            output->setEnabled(isFirstMode);
        }

        if (output->isEnabled()) {
            if(hadSetPrimary) {
                output->setPrimary(false);
            } else {
                hadSetPrimary = true;
                output->setPrimary(true);
            }
            Q_FOREACH(auto Mode, output->modes()){

                if (Mode->size().width()*Mode->size().height() < maxScreenSize) {
                    continue;
                } else if (Mode->size().width()*Mode->size().height() == maxScreenSize) {
                    if (refreshRate < Mode->refreshRate()) {
                        refreshRate = Mode->refreshRate();
                        output->setCurrentModeId(Mode->id());
                        USD_LOG(LOG_DEBUG,"use mode :%s %f",Mode->id().toLatin1().data(), Mode->refreshRate());
                    }
                    continue;
                }

                refreshRate = Mode->refreshRate();
                maxScreenSize = Mode->size().width()*Mode->size().height();
                output->setCurrentModeId(Mode->id());
                USD_LOG_SHOW_PARAM1(maxScreenSize);
            }
            output->setPos(QPoint(posX,0));
            posX+=output->size().width();
        }
        USD_LOG_SHOW_OUTPUT(output);
    }
    applyConfig();
}

void XrandrManager::setOutputsModeToExtend()
{
    int primaryX = 0;
    int screenSize = 0;
    int singleMaxWidth = 0;
    float refreshRate = 0.0;
    bool hadFindPrimay = false;
    int connectedScreens;

    if (false == checkPrimaryOutputsIsSetable()) {
        return;
    }

    if (readAndApplyOutputsModeFromConfig(UsdBaseClass::eScreenMode::extendScreenMode)) {
        return;
    }

    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (!output->isConnected()){
            continue;
        }
        connectedScreens++;
    }


    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {

            if (!output->isConnected()){
                continue;
            }
            if (hadFindPrimay) {
                output->setPrimary(false);
                continue;
            }
            if (!output->name().contains("eDP-1")) {//考虑   pnZXECRB项目中内屏为 DisplayPort-0
                output->setPrimary(false);
                continue;
            }

            hadFindPrimay = true;
            output->setPrimary(true);
            output->setEnabled(true);
            output->setRotation(static_cast<KScreen::Output::Rotation>(1));
            screenSize = 0;
            refreshRate = 0.0;

            Q_FOREACH(auto Mode, output->modes()) {
                if (Mode->size().width()*Mode->size().height() < screenSize){
                    continue;
                } else if (Mode->size().width()*Mode->size().height() == screenSize) {
                    if (Mode->refreshRate() <= refreshRate) {
                        continue;
                    }
                }

                refreshRate = Mode->refreshRate();
                screenSize = Mode->size().width()*Mode->size().height();
                output->setCurrentModeId(Mode->id());
                if (Mode->size().width() > singleMaxWidth) {
                    singleMaxWidth = Mode->size().width();
                }
            }

            output->setPos(QPoint(0,0));
            primaryX += singleMaxWidth;
            USD_LOG_SHOW_OUTPUT(output);
        }

    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        screenSize = 0;
        refreshRate = 0.0;
        singleMaxWidth = 0;

        if (output->isConnected()){
            output->setEnabled(true);
        } else {
            output->setEnabled(false);
            continue;
        }

        if (!hadFindPrimay) {
            output->setPrimary(true);
            hadFindPrimay = true;
        } else {
            if (output->isPrimary()){
                continue;
            }
        }

        output->setEnabled(true);
        output->setRotation(static_cast<KScreen::Output::Rotation>(1));

        Q_FOREACH(auto Mode, output->modes()){
            if (Mode->size().width()*Mode->size().height() < screenSize){
                continue;
            } else if (Mode->size().width()*Mode->size().height() == screenSize) {
                if (Mode->refreshRate() <= refreshRate) {
                    continue;
                }
            }

            refreshRate = Mode->refreshRate();
            screenSize = Mode->size().width()*Mode->size().height();
            output->setCurrentModeId(Mode->id());
            if (Mode->size().width() > singleMaxWidth) {
                singleMaxWidth = Mode->size().width();
            }
        }
        if (UsdBaseClass::isTablet()) {
            output->setRotation(static_cast<KScreen::Output::Rotation>(getCurrentRotation()));
        }

        output->setPos(QPoint(primaryX,0));
        primaryX += singleMaxWidth;
        USD_LOG_SHOW_OUTPUT(output);
    }
    applyConfig();
}

void XrandrManager::setOutputsParam(QString screensParam)
{
    USD_LOG(LOG_DEBUG,"param:%s", screensParam.toLatin1().data());
    std::unique_ptr<xrandrConfig> temp  = m_outputsConfig->readScreensConfigFromDbus(screensParam);
    if (nullptr != temp) {
        m_outputsConfig = std::move(temp);
    }
    applyConfig();
}

/*
 * 设置显示模式
*/
void XrandrManager::setOutputsMode(QString modeName)
{
    //检查当前屏幕数量，只有一个屏幕时不设置
    int screenConnectedCount = 0;
    int modeValue = m_outputModeEnum.keyToValue(modeName.toLatin1().data());
    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (true == output->isConnected()) {
            screenConnectedCount++;
        }
    }

    if(screenConnectedCount == 0) {
        return;
    }

    if(screenConnectedCount <= 1) {
        if (modeValue == UsdBaseClass::eScreenMode::cloneScreenMode ||
                 modeValue == UsdBaseClass::eScreenMode::extendScreenMode) {
            modeValue = UsdBaseClass::eScreenMode::firstScreenMode;
        }
    }

    switch (modeValue) {
    case UsdBaseClass::eScreenMode::cloneScreenMode:
        USD_LOG(LOG_DEBUG,"set mode to %s",modeName.toLatin1().data());
        setOutputsModeToClone();
        break;
    case UsdBaseClass::eScreenMode::firstScreenMode:
        USD_LOG(LOG_DEBUG,"set mode to %s",modeName.toLatin1().data());
        setOutputsModeToFirst(true);
        break;
    case UsdBaseClass::eScreenMode::secondScreenMode:
        USD_LOG(LOG_DEBUG,"set mode to %s",modeName.toLatin1().data());
        setOutputsModeToFirst(false);
        break;
    case UsdBaseClass::eScreenMode::extendScreenMode:
        USD_LOG(LOG_DEBUG,"set mode to %s",modeName.toLatin1().data());
        setOutputsModeToExtend();
        break;
    default:
        USD_LOG(LOG_DEBUG,"set mode fail can't set to %s",modeName.toLatin1().data());
        return;
    }
    sendOutputsModeToDbus();
}

/*
 * 识别当前显示的模式
*/
UsdBaseClass::eScreenMode XrandrManager::discernScreenMode()
{
    bool firstScreenIsEnable = false;
    bool secondScreenIsEnable = false;
    bool hadFindFirstScreen = false;

    QPoint firstScreenQPoint;
    QPoint secondScreenQPoint;

    QSize firstScreenQsize;
    QSize secondScreenQsize;

    Q_FOREACH(const KScreen::OutputPtr &output, m_outputsConfig->data()->outputs()) {
        if (output->isConnected()) {
            if (false == hadFindFirstScreen) {
                firstScreenIsEnable = output->isEnabled();
                if (output->isEnabled()  && output->currentMode()!=nullptr) {
                    firstScreenQsize = output->currentMode()->size();
                    firstScreenQPoint = output->pos();
                }
                hadFindFirstScreen = true;

            } else {
                secondScreenIsEnable = output->isEnabled();
                secondScreenQPoint = output->pos();
                if (secondScreenIsEnable && output->currentMode()!=nullptr) {
                    secondScreenQsize = output->currentMode()->size();
                }
                break;
            }
        }
    }

    if (true == firstScreenIsEnable && false == secondScreenIsEnable) {
        USD_LOG(LOG_DEBUG,"mode : firstScreenMode");
        return UsdBaseClass::eScreenMode::firstScreenMode;
    }

    if (false == firstScreenIsEnable && true == secondScreenIsEnable) {
        USD_LOG(LOG_DEBUG,"mode : secondScreenMode");
        return UsdBaseClass::eScreenMode::secondScreenMode;
    }

    if (firstScreenQPoint == secondScreenQPoint && firstScreenQsize==secondScreenQsize) {
        USD_LOG(LOG_DEBUG,"mode : cloneScreenMode");
        return UsdBaseClass::eScreenMode::cloneScreenMode;
    }

    USD_LOG(LOG_DEBUG,"mode : extendScreenMode");
    return UsdBaseClass::eScreenMode::extendScreenMode;
}

void XrandrManager::doCalibrate(const QString screenMap)
{
    USD_LOG(LOG_DEBUG,"controlScreenMap ...");
    doRotationChanged(screenMap);
}

void XrandrManager::disableCrtc()
{
    int tempInt;

    Display	*m_pDpy;
    Window	m_rootWindow;
    XRRScreenResources  *m_pScreenRes;
    int m_screen;
    m_pDpy = XOpenDisplay (NULL);
    if (m_pDpy == NULL) {
        USD_LOG(LOG_DEBUG,"XOpenDisplay fail...");
        return ;
    }

    m_screen = DefaultScreen(m_pDpy);
    if (m_screen >= ScreenCount (m_pDpy)) {
        USD_LOG(LOG_DEBUG,"Invalid screen number %d (display has %d)",m_screen, ScreenCount(m_pDpy));
        return ;
    }

    m_rootWindow = RootWindow(m_pDpy, m_screen);
    m_pScreenRes = XRRGetScreenResources(m_pDpy, m_rootWindow);
    if (NULL == m_pScreenRes) {
        USD_LOG(LOG_DEBUG,"could not get screen resources",m_screen, ScreenCount(m_pDpy));
        return ;
    }
    if (m_pScreenRes->noutput == 0) {
        USD_LOG(LOG_DEBUG, "noutput is 0!!");
        return ;
    }
    USD_LOG(LOG_DEBUG,"initXparam success");
    for (tempInt = 0; tempInt < m_pScreenRes->ncrtc; tempInt++) {
        int ret = 0;
        ret = XRRSetCrtcConfig (m_pDpy, m_pScreenRes, m_pScreenRes->crtcs[tempInt], CurrentTime,
                                0, 0, None, RR_Rotate_0, NULL, 0);
        if (ret != RRSetConfigSuccess) {
            USD_LOG(LOG_ERR,"disable crtc:%d error! ");
        }
    }
    XCloseDisplay(m_pDpy);
    USD_LOG(LOG_DEBUG,"disable crtc  success");
}

/**
 * @brief XrandrManager::StartXrandrIdleCb
 * 开始时间回调函数
 */
void XrandrManager::active()
{
    m_acitveTimer->stop();
    m_screenSignalTimer = new QTimer(this);
    connect(m_screenSignalTimer, SIGNAL(timeout()), this, SLOT(doSaveConfigTimeOut()));
    USD_LOG(LOG_DEBUG,"StartXrandrIdleCb ok.");
    //    QMetaObject::invokeMethod(this, "getInitialConfig", Qt::QueuedConnection);
    connect(m_outputsInitTimer,  SIGNAL(timeout()), this, SLOT(getInitialConfig()));
    m_outputsInitTimer->start(0);
    connect(m_xrandrDbus, SIGNAL(setScreenModeSignal(QString)), this, SLOT(setOutputsMode(QString)));
    connect(m_xrandrDbus, SIGNAL(setScreensParamSignal(QString)), this, SLOT(setOutputsParam(QString)));

#if 0
    QDBusInterface *modeChangedSignalHandle = new QDBusInterface(DBUS_XRANDR_NAME,DBUS_XRANDR_PATH,DBUS_XRANDR_INTERFACE,QDBusConnection::sessionBus(),this);

    if (modeChangedSignalHandle->isValid()) {
        connect(modeChangedSignalHandle, SIGNAL(screenModeChanged(int)), this, SLOT(screenModeChangedSignal(int)));

    } else {
        USD_LOG(LOG_ERR, "modeChangedSignalHandle");
    }

    QDBusInterface *screensChangedSignalHandle = new QDBusInterface(DBUS_XRANDR_NAME,DBUS_XRANDR_PATH,DBUS_XRANDR_INTERFACE,QDBusConnection::sessionBus(),this);

     if (screensChangedSignalHandle->isValid()) {
         connect(screensChangedSignalHandle, SIGNAL(screensParamChanged(QString)), this, SLOT(screensParamChangedSignal(QString)));
         //USD_LOG(LOG_DEBUG, "..");
     } else {
         USD_LOG(LOG_ERR, "screensChangedSignalHandle");
     }
#endif

}
