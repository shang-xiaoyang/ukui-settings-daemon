#ifndef UKUISCALEMANAGER_H
#define UKUISCALEMANAGER_H

#include <QObject>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QMessageBox>
#include <QTimer>
#include <QGSettings/qgsettings.h>

#include "ukui-gpu-xrandr.h"
#include "ukui-scale-dbus.h"

#include <X11/Xlibint.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h>

class UkuiScaleManager : public QObject
{
    Q_OBJECT
public:
    UkuiScaleManager(QObject *parent = nullptr);
    ~UkuiScaleManager();

public:
    void setScreenMode(QString screenName, QRect geometry, bool primary, int rotation, double scale)
    {
        mScreenName = screenName;
        mGeometry   = geometry;
        mPrimary    = primary;
        mRotation   = rotation;
        mScale      = scale;
    }
    void UkuiScaleManagerSetScreenMapList(QList<QMap<QString, QVariant>> map)
    {
        if (!mScreenMapList.isEmpty())
            mScreenMapList.clear();

        mScreenMapList = map;
    }
    void UKuiScaleManagerReadConfig();
    void UkuiScaleManagerScaleApply();
    bool UkuiScaleManagerJudginTheCurrentZoom(QList<UkuiCrtcConfig *>  *m_configList);
    void UkuiScaleManagerShowZoomtips();
    void UKuiScaleManagerSetCrtcConfig(QList<UkuiCrtcConfig *>  *m_configList);
    void UkuiScaleManagerUpdateScreenSize (Display *xdisplay, int screen);
    void UkuiScaleManagerSetScale (UkuiCrtcConfig *cConfig, Display *xdisplay);
    bool UkuiScaleManagerSetScreenSize();
    void UkuiScaleManagerInitTransform(XTransform *transform);
    void UkuiScaleManagerGetTransform(Display *xDisplay);

    void getInitialConfig();

public Q_SLOTS:
    void screenScaleChange(QString screenName);
    void UkuiScaleManagerScreenScaleChange(QStringList stringList);


private:
    QTimer     *mKscreenInitTimer = nullptr;

    QGSettings *mSettings;
    QString     mScreenName;
    QRect       mGeometry;

    bool        mPrimary;
    int         mRotation;
    double      mScale;

    int         fb_width = 0;
    int         fb_height = 0;
    int         fb_width_mm = 0;
    int         fb_height_mm = 0;

    XTransform          transform;
    XRRScreenResources  *mResources;
    UkuiScaleDbus       *mDbus;
    UkuiGpuXrandr       *mGpu;
    QList<QMap<QString, QVariant>> mScreenMapList;
};


#endif // UKUISCALEMANAGER_H
