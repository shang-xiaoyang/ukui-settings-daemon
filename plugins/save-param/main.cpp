/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2019 Tianjin KYLIN Information Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>
#include <QString>
#include <QFileInfo>

#include "save-screen.h"
#include "clib-syslog.h"

#include <QGSettings/QGSettings>
extern "C" {
#include <X11/Xatom.h>
#include <X11/Xlib.h>
}


#define XSETTINGS_SCHEMA    "org.ukui.SettingsDaemon.plugins.xsettings"
#define MOUSE_SCHEMA        "org.ukui.peripherals-mouse"
#define SCALING_KEY         "scaling-factor"
#define CURSOR_SIZE         "cursor-size"
#define CURSOR_THEME        "cursor-theme"

bool g_isGet = false;
bool g_isSet = false;

/* 设置DPI环境变量 */
void setXresources(double scale)
{
    Display    *dpy;
    QGSettings *mouse_settings = new QGSettings(MOUSE_SCHEMA);
    QString str = QString("Xft.dpi:\t%1\nXcursor.size:\t%2\nXcursor.theme:\t%3\n")
                         .arg(scale * 96)
                         .arg(mouse_settings->get(CURSOR_SIZE).toInt() * scale)
                         .arg(mouse_settings->get(CURSOR_THEME).toString());

    dpy = XOpenDisplay(NULL);
    XChangeProperty(dpy, RootWindow(dpy, 0), XA_RESOURCE_MANAGER, XA_STRING, 8,
                    PropModeReplace, (unsigned char *) str.toLatin1().data(), str.length());
    XCloseDisplay(dpy);

    qDebug() << "setXresources：" << str;

    delete mouse_settings;
}

/* 判断文件是否存在 */
bool isFileExist(QString XresourcesFile)
{
    QFileInfo fileInfo(XresourcesFile);
    if (fileInfo.isFile()) {
        qDebug() << "File exists";
        return true;
    }

    qDebug() << "File does not exis";

    return false;
}

/* 编写判断标志文件，更改 鼠标/DPI 配置大小*/
void writeXresourcesFile(QString XresourcesFile, QGSettings *settings, double scaling)
{
    QFile file(XresourcesFile);
    QString content = QString("Xft.dpi:%1\nXcursor.size:%2").arg(96.0 * scaling).arg(24.0 * scaling);
    QByteArray str = content.toLatin1().data();

    file.open(QIODevice::ReadWrite | QIODevice::Text);
    file.write(str);
    file.close();

    QGSettings *Font = new QGSettings("org.ukui.font-rendering");

    Font->set("dpi", 96.0);
    //settings->set(SCALING_KEY, scaling);

    qDebug() << " writeXresourcesFile: content = " << content
             << " scalings = " << settings->get(SCALING_KEY).toDouble();
    delete Font;
}

/* 判断是否为首次登陆 */

bool isTheFirstLogin(QGSettings *settings)
{
    QString homePath       = getenv("HOME");
    QString XresourcesFile = homePath+"/.config/xresources";
    QString Xresources     = homePath+"/.Xresources";
    qreal   scaling        = qApp->devicePixelRatio();
    bool    zoom1 = false, zoom2 = false, zoom3 = false;
    double  mScaling;
    bool xres, Xres;

    Xres = isFileExist(Xresources);
    xres = isFileExist(XresourcesFile); //判断标志文件是否存在

    if (xres && !Xres) {
        return false;
    } else if (xres && Xres) {
        QFile::remove(Xresources);
        return false;
    } else if (Xres && !xres) {
        QFile::rename(Xresources, XresourcesFile);
        return false;
    }

    for (QScreen *screen : QGuiApplication::screens()) {
        int width  = screen->geometry().width() * scaling;
        int height = screen->geometry().height() * scaling;

        if (width <= 1920 && height <= 1080) {
            zoom1 = true;
        }
        else //if (width > 1920 && height > 1080 && width <= 2560 && height <=1500) {
            zoom2 = true;
        //} else if (width > 2560 && height > 1440) {
        //    zoom3 = true;
        //}
        syslog(LOG_ERR, "USD: ---- main:  width = %d, height = %d, zoom1 = %d, zoom2 = %d", width,height,zoom1,zoom2);
    }

    if (zoom1 && !zoom2) {
        mScaling = 1.0;
    }
    else// if (!zoom1 && zoom2) {
        mScaling = 2.0;
    //} else if (!zoom1 && !zoom2 && zoom3) {
    //    mScaling = 2.0;
    //}

    settings->set(SCALING_KEY, mScaling);
    writeXresourcesFile(XresourcesFile, settings, mScaling);

    return true;
}

/* 配置新装系统、新建用户第一次登陆时，4K缩放功能*/
void setHightResolutionScreenZoom()
{
    QGSettings *settings;
    double      scale = 1.0;

    if (!QGSettings::isSchemaInstalled(XSETTINGS_SCHEMA) || !QGSettings::isSchemaInstalled("org.ukui.font-rendering") ||
            !QGSettings::isSchemaInstalled(MOUSE_SCHEMA)) {
        qDebug() << "Error: ukui-settings-daemon's Schema  is not installed, will not setting dpi!";
        delete settings;
        return;
    }
    settings = new QGSettings(XSETTINGS_SCHEMA);

    if (isTheFirstLogin(settings)) {
        qDebug() << "Set the default zoom value when logging in for the first time.";
    }

    scale = settings->get(SCALING_KEY).toDouble();
    setXresources(scale);

    delete settings;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("save-param");
    QApplication::setApplicationVersion("1.0.0");

    setHightResolutionScreenZoom();

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main", "Qt"));
    parser.addHelpOption();  // 添加帮助选项 （"-h" 或 "--help"）
    parser.addVersionOption();  // 添加版本选项 ("-v" 或 "--version")
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);

    QCommandLineOption setOption(QStringList() << "s" << "set",
                                       QApplication::translate("main", "Get the display parameters of the current user and save them to the lightdm user directory"));
    parser.addOption(setOption);
    QCommandLineOption getOption(QStringList() << "g" << "get",
                                       QApplication::translate("main", "Get the display parameters of the current user and save them to the user directory of lightdm"));

    parser.addOption(getOption);

    QCommandLineOption userOption(QStringList() << "u" << "user",
                                       QApplication::translate("main", "Get the display parameters saved by the user in the lightdm personal folder and set them"),
                                       QApplication::translate("main", "user"), "");

    parser.addOption(userOption);

    QCommandLineOption cloneOption(QStringList() << "c" << "clone",
                                       QApplication::translate("main", "Set the screen to clone mode"));

    parser.addOption(cloneOption);

    parser.setApplicationDescription(QGuiApplication::translate("main", "Qt"));  // 设置应用程序描述信息


    parser.process(app);

    SaveScreenParam saveParam;

    if(parser.isSet(setOption)) {
        SYS_LOG(LOG_DEBUG,".");
        saveParam.setIsSet(true);
    } else if (parser.isSet(getOption)) {
        SYS_LOG(LOG_DEBUG,".");
        saveParam.setIsGet(true);
    } else if (parser.isSet(userOption)){
        QString user = parser.value(userOption);
        saveParam.setUserName(user);
        saveParam.setUserConfigParam();
    } else if (parser.isSet(cloneOption)){
//        saveParam.setClone();
    }

//    saveParam.setClone();
    saveParam.getConfig();
    return app.exec();
}
