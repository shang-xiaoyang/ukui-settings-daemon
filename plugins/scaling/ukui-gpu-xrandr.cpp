#include <QDebug>
#include "ukui-gpu-xrandr.h"

#define SETTINGS_SCHEMAS "org.ukui.scalings.xsettings"
#define SCALING_KEY      "scalingFactor"

UkuiGpuXrandr::UkuiGpuXrandr()
{
    resources = NULL;
    xdisplay  = NULL;
    m_crtcConfig = new QList<UkuiCrtcConfig *>;

    settings = new QGSettings(SETTINGS_SCHEMAS);
    xdisplay = XOpenDisplay(0);
    connect(settings, &QGSettings::changed,
            this, &UkuiGpuXrandr::ukuiGpuXrandrGsettingsChange);

}

UkuiGpuXrandr::~UkuiGpuXrandr()
{
    if(settings)
        delete settings;
    if (resources)
        XRRFreeScreenResources (resources);
}

QString get_xmode_name (XRRModeInfo *xmode)
{
    int width = xmode->width;
    int height = xmode->height;

    QString str = QString("%1x%2").arg(width).arg(height);

    return str;
}

void UkuiGpuXrandr::ukuiGpuXrandrGetScreenSize()
{
    XRRGetScreenSizeRange (xdisplay, DefaultRootWindow (xdisplay),
                         &min_width,
                         &min_height,
                         &max_width,
                         &max_height);
}

bool UkuiGpuXrandr::ukuiGpuXrandrReadCurrent()
{
    XRRScreenResources *resources;
    RROutput primary_output;

    const char *xdisplay_name;
    xdisplay_name = g_getenv ("DISPLAY");

    if (xdisplay)
        XCloseDisplay(xdisplay);

    if (this->resources)
        XRRFreeScreenResources (this->resources);

    xdisplay = XOpenDisplay(xdisplay_name);

    this->resources = NULL;
    m_crtcConfig->clear();

    ukuiGpuXrandrGetScreenSize();

    resources = XRRGetScreenResources (xdisplay,
                                       DefaultRootWindow (xdisplay));
    if (!resources)
    {
        qDebug("Failed to retrieve Xrandr screen resources");
        return false;
    }

    this->resources = resources;

    for (int i = 0; i < (unsigned)resources->nmode; i++)
    {
        glong mode_id;
        QString name;
        unsigned int width;
        unsigned int height;
        double refresh_rate;
        XRRModeInfo *xmode = &resources->modes[i];
        mode_id = (glong)xmode->id;
        width = xmode->width;
        height = xmode->height;

        refresh_rate = (double)(xmode->dotClock /
                      ((double)xmode->hTotal * xmode->vTotal));
        name = get_xmode_name (xmode);

    }
    for (int i = 0; i < (unsigned)resources->ncrtc; i++)
    {
        XRRCrtcInfo *xrandr_crtc;
        XRRCrtcTransformAttributes *transform_attributes;
        RRCrtc crtc_id;
        UkuiCrtcConfig *cConfig;

        crtc_id = resources->crtcs[i];
        xrandr_crtc = XRRGetCrtcInfo (xdisplay,
                                      resources,
                                      crtc_id);
        if (!XRRGetCrtcTransform (xdisplay, crtc_id, &transform_attributes))
            transform_attributes = NULL;

        if(xrandr_crtc->width == 0 || xrandr_crtc->height ==0){
            continue;
        }

        cConfig = new UkuiCrtcConfig();
        cConfig->crtc_info = xrandr_crtc;
        cConfig->crtc      = crtc_id;
        cConfig->ncrtc     = i;
        cConfig->geometry  = QRect(xrandr_crtc->x,xrandr_crtc->y,
                                   xrandr_crtc->width,xrandr_crtc->height);

        m_crtcConfig->append(cConfig);

        XFree (transform_attributes);
    }
    primary_output = XRRGetOutputPrimary (xdisplay,
                                          DefaultRootWindow (xdisplay));

    for (int i = 0; i < (unsigned)resources->noutput; i++)
    {
        RROutput output_id;
        XRROutputInfo *xrandr_output;
        output_id = resources->outputs[i];
        xrandr_output = XRRGetOutputInfo (xdisplay,
                                        resources, output_id);

        if (!xrandr_output)
            continue;

        if (xrandr_output->connection == RR_Connected)
        {
            for(int j = 0; j < m_crtcConfig->length(); j++){
                UkuiCrtcConfig *cConfig = m_crtcConfig->at(j);
                if (cConfig->crtc == xrandr_output->crtc){
                    cConfig->xrandr_output = xrandr_output;
                    if (output_id == primary_output)
                        cConfig->isPrimary = true;
                    else
                        cConfig->isPrimary = false;
                }
            }
        }

    }
    return true;
}

void UkuiGpuXrandr::ukuiGpuXrandrGsettingsChange(const QString &key)
{
    qDebug()<<key;
    if (key.compare(SCALING_KEY) == 0){
        //ukuiGpuXrandrReadCurrent();
    }
}
