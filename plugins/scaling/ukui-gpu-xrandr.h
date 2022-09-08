#ifndef UKUIGPUXRANDR_H
#define UKUIGPUXRANDR_H

#include <QObject>
#include <QList>
#include <QGSettings/qgsettings.h>

#include <glib.h>
#include <string.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xlibint.h>

class UkuiCrtcConfig
{
public:
    UkuiCrtcConfig() {
        memset (&mTransform, '\0', sizeof (mTransform));
        memset (&current_transform, '\0', sizeof (current_transform));
        for (int x = 0; x < 3 ; x++){
            mTransform.matrix[x][x] = XDoubleToFixed (1.0);
            current_transform.matrix[x][x] = XDoubleToFixed (1.0);
        }
    }

public:
    int            ncrtc;
    RRCrtc         crtc;
    RRMode         mode;
    RROutput      *outputs;
    int            noutputs;
    XRRCrtcInfo   *crtc_info;
    bool           isPrimary;
    XRROutputInfo *xrandr_output;
    QRect          geometry;
    double         scaling = 1.0;
    double         otherScaling = 1.0;
    XTransform     mTransform;
    XTransform     current_transform;
};

class UkuiGpuXrandr : public QObject
{
    Q_OBJECT
public:
    UkuiGpuXrandr();
    ~UkuiGpuXrandr();

public:
    bool ukuiGpuXrandrReadCurrent();
    void ukuiGpuXrandrGsettingsChange(const QString &key);
    void ukuiGpuXrandrGetScreenSize();
    XRRScreenResources *ukuiGpuGetScreenResources()
    {
        return resources;
    }

    Display *ukuiGpuGetDisplay()
    {
        return xdisplay;
    }

    QList<UkuiCrtcConfig *> *ukuiGpuGetConfigList()
    {
        return m_crtcConfig;
    }

public:
    QList<UkuiCrtcConfig *>  *m_crtcConfig;
    int     min_width;
    int     min_height;
    int     max_width;
    int     max_height;

private:
    QGSettings         *settings;
    Display            *xdisplay;
    XRRScreenResources *resources;

};

#endif // UKUIGPUXRANDR_H
