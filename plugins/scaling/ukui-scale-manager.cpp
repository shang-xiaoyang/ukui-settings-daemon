#include "ukui-scale-adaptor.h"
#include <QApplication>
#include <QDebug>
#include <QDBusMessage>
#include <QScreen>
#include "ukui-scale-manager.h"

#define XSETTINGS_PLUGIN_SCHEMA "org.ukui.SettingsDaemon.plugins.xsettings"
#define SCALING_FACTOR_KEY      "scaling-factor"
#define DPI_FALLBACK 96.0

UkuiScaleManager::UkuiScaleManager(QObject *parent) : QObject(parent)
{
    mGpu = new UkuiGpuXrandr();
    mDbus = new UkuiScaleDbus(this);
    mSettings = new QGSettings(XSETTINGS_PLUGIN_SCHEMA);
    mScale = mSettings->get(SCALING_FACTOR_KEY).toDouble();
    mKscreenInitTimer = new QTimer(this);

    new ScaleAdaptor(mDbus);

    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (sessionBus.registerService(DBUS_SCALE_NAME)) {
        sessionBus.registerObject(USD_SCALE_DBUS_PATH,
                                  mDbus,
                                  QDBusConnection::ExportAllContents);
    }
    UKuiScaleManagerReadConfig();
    connect(mDbus, &UkuiScaleDbus::setScreenModeSignal, this, &UkuiScaleManager::screenScaleChange);
    connect(mDbus, &UkuiScaleDbus::setNumScreenModeSignal, this, &UkuiScaleManager::UkuiScaleManagerScreenScaleChange);
}

UkuiScaleManager::~UkuiScaleManager()
{
    if(mDbus)
        delete mDbus;
    if(mSettings)
        delete mSettings;
    if (mGpu)
        delete mGpu;
    delete mKscreenInitTimer;
}

void UkuiScaleManager::UKuiScaleManagerReadConfig()
{
    QDir dir;
    QString FilePath = dir.homePath() + "/.config/scaling.cfg";
    QFile file;
    QMap<QString, QVariant> screenMap;
    file.setFileName(FilePath);

    if (!mScreenMapList.isEmpty())
        mScreenMapList.clear();

    if(file.open(QIODevice::ReadOnly | QIODevice::Text)){
        while (!file.atEnd()) {
            QByteArray line = file.readLine();
            QString str(line);
            QString geometryStr;
            QStringList screenDate, geometryDate;
            double scale;
            int x, y, w, h;

            screenDate = str.split(":");
            geometryStr = screenDate.at(1);
            geometryDate = geometryStr.split(",");

            x = geometryDate.at(0).toInt();
            y = geometryDate.at(1).toInt();
            w = geometryDate.at(2).toInt();
            h = geometryDate.at(3).toInt();

            scale = screenDate.at(4).toDouble();
            scale = qCeil(scale) - scale + qFloor(scale);

            screenMap.insert("screenName", screenDate.at(0));
            screenMap.insert("geometry", QRect(x,y,w,h));
            screenMap.insert("primary", screenDate.at(2));
            screenMap.insert("rotation", screenDate.at(3));
            screenMap.insert("scale", scale);
            mScreenMapList.append(screenMap);
        }
    }
    file.close();

    for(QMap<QString, QVariant> map : mScreenMapList)
    {
        qDebug()<<"screenName = "<<map["screenName"].toString();
        qDebug()<<"geometry = "<<map["geometry"].toRect();
        qDebug()<<"primary = "<<map["primary"].toBool();
        qDebug()<<"rotation = "<<map["rotation"].toInt();
        qDebug()<<"scale = "<<map["scale"].toDouble();
    }
}

void UkuiScaleManager::UkuiScaleManagerUpdateScreenSize (Display *xdisplay, int screen)
{
    if (fb_width == DisplayWidth (xdisplay, screen) &&
        fb_height == DisplayHeight (xdisplay, screen) &&
        fb_width_mm == DisplayWidthMM (xdisplay, screen) &&
        fb_height_mm == DisplayHeightMM (xdisplay, screen))
        {
            return;
        }

    XRRSetScreenSize (xdisplay, RootWindow (xdisplay, screen), fb_width, fb_height,
                      fb_width_mm, fb_height_mm);

}

void UkuiScaleManager::UkuiScaleManagerInitTransform(XTransform *transform)
{
    memset (&transform, '\0', sizeof (transform));
    for (int x = 0; x < 3; x++)
        transform->matrix[x][x] = XDoubleToFixed (1.0);
}

void UkuiScaleManager::UkuiScaleManagerGetTransform(Display *xDisplay)
{
    QList<UkuiCrtcConfig *>  *m_configList;
    m_configList = mGpu->ukuiGpuGetConfigList();

    for(int i = 0; i < m_configList->length(); i++)
    {
        UkuiCrtcConfig *cConfig = m_configList->at(i);
        XRRCrtcInfo *crtc_info = cConfig->crtc_info;
        XRRCrtcTransformAttributes  *attr;
        if (!crtc_info)
            qDebug ("could not get crtc 0x%lx information\n", cConfig->crtc);
        if (XRRGetCrtcTransform (xDisplay, cConfig->crtc, &attr) && attr) {
            cConfig->current_transform = attr->currentTransform;
        }
        else {
            UkuiScaleManagerInitTransform (&cConfig->current_transform);
        }
    }
}

void UkuiScaleManager::UkuiScaleManagerSetScale(UkuiCrtcConfig *cConfig, Display *xdisplay)
{
    const char *scale_filter;
    int	major, minor;

    if (cConfig->scaling != 1)
        scale_filter = FilterBilinear;/*"bilinear";*/
    else
        scale_filter = FilterNearest;//"nearest";

    XRRQueryVersion (xdisplay, &major, &minor);

    if (major > 1 || (major == 1 && minor >= 3))
        XRRSetCrtcTransform (xdisplay, cConfig->crtc,
                         &cConfig->mTransform,
                         scale_filter,
                         0,
                         NULL);
}

static int
mode_height (QRect mode_info, Rotation rotation)
{
    switch (rotation & 0xf) {
    case RR_Rotate_0:
    case RR_Rotate_180:
    return mode_info.height();
    case RR_Rotate_90:
    case RR_Rotate_270:
    return mode_info.width();
    default:
    return 0;
    }
}

static int
mode_width (QRect mode_info, Rotation rotation)
{

    switch (rotation & 0xf) {
    case RR_Rotate_0:
    case RR_Rotate_180:
    return mode_info.width();
    case RR_Rotate_90:
    case RR_Rotate_270:
    return mode_info.height();
    default:
    return 0;
    }
}

bool UkuiScaleManager::UkuiScaleManagerSetScreenSize()
{
    bool fb_specified = fb_width != 0 && fb_height != 0;

    QList<UkuiCrtcConfig *>  *m_configList = mGpu->ukuiGpuGetConfigList();

    for (int i = 0; i < m_configList->length(); i++)
    {
        UkuiCrtcConfig *cConfig = m_configList->at(i);
        XRRCrtcInfo *crtc_info = cConfig->crtc_info;
        int	    x, y, w, h;

        if (!crtc_info) continue;

        w = mode_width(cConfig->geometry, cConfig->crtc_info->rotation) * cConfig->scaling;
        h = mode_height(cConfig->geometry, cConfig->crtc_info->rotation) * cConfig->scaling;
        if (crtc_info->x != 0)
            x = cConfig->geometry.x() * cConfig->otherScaling;
        else
            x = 0;

        if (crtc_info->y != 0)
            y = cConfig->geometry.y() * cConfig->otherScaling;
        else
            y = 0;

        qDebug("x = %d, y = %d, w = %d, h = %d", x, y, w, h);
        /* make sure output fits in specified size */
        if (fb_specified)
        {
            if (x + w > fb_width || y + h > fb_height)
            qWarning ("specified screen %dx%d not large enough for output %s (%dx%d+%d+%d)\n",
                fb_width, fb_height, cConfig->xrandr_output->name, w, h, x, y);
        }
        /* fit fb to output */
        else
        {
            if (x + w > fb_width)
                fb_width = x + w;
            if (y + h > fb_height)
                fb_height = y + h;
        }
    }
    if (fb_specified)
    {
        if (fb_width > mGpu->max_width || fb_height > mGpu->max_height)
            qDebug ("screen cannot be larger than %dx%d (desired size %dx%d)\n",
               mGpu->max_width, mGpu->max_height, fb_width, fb_height);
        if (fb_width < mGpu->min_width || fb_height < mGpu->min_height)
            qDebug("screen must be at least %dx%d\n", mGpu->min_width, mGpu->min_height);
    }
    else
    {
        if (fb_width < mGpu->min_width)
            fb_width = mGpu->min_width;
        if (fb_height < mGpu->min_height)
            fb_height = mGpu->min_height;
    }
    return true;
}

void UkuiScaleManager::UKuiScaleManagerSetCrtcConfig(QList<UkuiCrtcConfig *>  *m_configList)
{
    for(int i = 0; i < m_configList->length(); i++)
    {
        UkuiCrtcConfig *cConfig = m_configList->at(i);
        for(QMap<QString, QVariant> map : mScreenMapList)
        {
            if (map["screenName"].toString().compare(cConfig->xrandr_output->name) == 0)
            {
                cConfig->geometry = map["geometry"].toRect();

                if (mScale == 2 && map["scale"].toDouble() == 1)
                    cConfig->scaling  = 2;
                else if (mScale == 2 && map["scale"].toDouble() == 2)
                    cConfig->scaling  = 1;
                else
                    cConfig->scaling  = map["scale"].toDouble();

                cConfig->mTransform.matrix[0][0] = XDoubleToFixed (cConfig->scaling);
                cConfig->mTransform.matrix[1][1] = XDoubleToFixed (cConfig->scaling);
                cConfig->mTransform.matrix[2][2] = XDoubleToFixed (1.0);
            }
            else {
                if (mScale == 2 && map["scale"].toDouble() == 1)
                    cConfig->otherScaling  = 2;
                else if (mScale == 2 && map["scale"].toDouble() == 2)
                    cConfig->otherScaling  = 1;
                else
                    cConfig->otherScaling = map["scale"].toDouble();
            }
        }
    }
}

void UkuiScaleManager::UkuiScaleManagerShowZoomtips()
{
    int ret;
    QDBusInterface ifc("org.gnome.SessionManager",
                       "/org/gnome/SessionManager",
                       "org.gnome.SessionManager",
                       QDBusConnection::sessionBus());
    QMessageBox msg;
    msg.setWindowTitle(tr("Hint"));
    msg.setText(tr("The first time you set the zoom, you need to log out to take effect"));
    msg.addButton(tr("Log out now"), QMessageBox::AcceptRole);
    msg.addButton(tr("Later"), QMessageBox::RejectRole);

    ret = msg.exec();

    switch (ret) {
    case QMessageBox::AcceptRole:
        ifc.call("logout");
        break;
    case QMessageBox::RejectRole:
        break;
    }
}

bool UkuiScaleManager::UkuiScaleManagerJudginTheCurrentZoom(QList<UkuiCrtcConfig *>  *m_configList)
{
    qDebug()<<"mScale == "<<mScale;
    bool scale_flag = false;
    for(int i = 0; i < m_configList->length(); i++)
    {
        UkuiCrtcConfig *cConfig = m_configList->at(i);
        if(cConfig->scaling != 1 && mScale == 1)
        {
            scale_flag = true;
            cConfig->scaling = 1;
        }
    }
    if(scale_flag){
        mSettings->set(SCALING_FACTOR_KEY, 2);
        UkuiScaleManagerShowZoomtips();
        return false;
    }
    return true;

}

void UkuiScaleManager::getInitialConfig()
{
    connect(mKscreenInitTimer,  &QTimer::timeout, this, &UkuiScaleManager::UkuiScaleManagerScaleApply);
    mKscreenInitTimer->start(2000);
}

void UkuiScaleManager::UkuiScaleManagerScaleApply()
{
    int i;
    int w, h, x, y;
    double	dpi = 0;
    Status  s;
    Display *xDisplay;
    QList<UkuiCrtcConfig *>  *m_configList;
    mKscreenInitTimer->stop();
    mGpu->ukuiGpuXrandrReadCurrent();
    xDisplay = mGpu->ukuiGpuGetDisplay();
    mResources = mGpu->ukuiGpuGetScreenResources();
    m_configList = mGpu->ukuiGpuGetConfigList();

    UkuiScaleManagerGetTransform(xDisplay);

    UKuiScaleManagerSetCrtcConfig(m_configList);

    bool res = UkuiScaleManagerJudginTheCurrentZoom(m_configList);

    if(!res)
        return;
    UkuiScaleManagerSetScreenSize();

    int	screen = DefaultScreen(xDisplay);
    if (fb_width_mm == 0 || fb_height_mm == 0)
    {
        if (fb_width != DisplayWidth (xDisplay, screen) ||
            fb_height != DisplayHeight (xDisplay, screen) || dpi != 0.0)
        {
            if (dpi <= 0)
                dpi = (25.4 * DisplayHeight (xDisplay, screen)) / DisplayHeightMM(xDisplay, screen);

            fb_width_mm = (25.4 * fb_width) / dpi;
            fb_height_mm = (25.4 * fb_height) / dpi;
        }
        else {
            fb_width_mm = DisplayWidthMM (xDisplay, screen);
            fb_height_mm = DisplayHeightMM (xDisplay, screen);
        }
    }

    XGrabServer (xDisplay);
    for(i = 0; i < m_configList->length(); i++)
    {
        UkuiCrtcConfig *cConfig = m_configList->at(i);
        XRRCrtcInfo *crtc_info = cConfig->crtc_info;
        /* if this crtc is already disabled, skip it */
        if (crtc_info->mode == None)
            continue;

        s = XRRSetCrtcConfig (xDisplay, mResources, cConfig->crtc, CurrentTime,
                              0, 0, None, RR_Rotate_0, NULL, 0);
    }

    UkuiScaleManagerUpdateScreenSize(xDisplay, screen);

    for(i = 0; i < m_configList->length(); i++)
    {
        UkuiCrtcConfig *cConfig = m_configList->at(i);
        XRRCrtcInfo *crtc_info = cConfig->crtc_info;
        if (crtc_info->mode == None)
            continue;
        if(crtc_info->width == 0 || crtc_info->height ==0)
            continue;
        w = (cConfig->geometry.x() + cConfig->geometry.width() )* cConfig->scaling;
        h = (cConfig->geometry.y() + cConfig->geometry.height() )* cConfig->scaling;

        if (crtc_info->x != 0)
            x = cConfig->geometry.x() * cConfig->otherScaling;
        else
            x = 0;

        if (crtc_info->y != 0)
            y = cConfig->geometry.y() * cConfig->otherScaling;
        else
            y = 0;

        UkuiScaleManagerSetScale(cConfig, xDisplay);
        XRRSetCrtcConfig (xDisplay, mResources, cConfig->crtc, CurrentTime,
                          x, y, crtc_info->mode, crtc_info->rotation,
                          crtc_info->outputs, crtc_info->noutput);
    }
    XUngrabServer (xDisplay);
    XSync (xDisplay, False);
}

void UkuiScaleManager::screenScaleChange(QString screenName)
{
    fb_width = 0;
    fb_height = 0;
    fb_width_mm = 0;
    fb_height_mm = 0;
    UkuiScaleManagerScaleApply();
}

void UkuiScaleManager::UkuiScaleManagerScreenScaleChange(QStringList nameList)
{
    for(QMap<QString, QVariant> map : mScreenMapList)
    {
        qDebug()<<"mScreenMapList--------------------- = "<<mScreenMapList.length();
        qDebug()<<"screenName = "<<map["screenName"].toString();
        qDebug()<<"geometry = "<<map["geometry"].toRect();
        qDebug()<<"primary = "<<map["primary"].toBool();
        qDebug()<<"rotation = "<<map["rotation"].toInt();
        qDebug()<<"scale = "<<map["scale"].toDouble();
    }
    fb_width = 0;
    fb_height = 0;
    fb_width_mm = 0;
    fb_height_mm = 0;
    UkuiScaleManagerScaleApply();
}
