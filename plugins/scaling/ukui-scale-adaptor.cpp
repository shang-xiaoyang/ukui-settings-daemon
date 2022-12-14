/*
 * This file was generated by qdbusxml2cpp version 0.8
 * Command line was: qdbusxml2cpp ukui-scale-dbus.xml -a ukui-scale-adaptor
 *
 * qdbusxml2cpp is Copyright (C) 2020 The Qt Company Ltd.
 *
 * This is an auto-generated file.
 * Do not edit! All changes made to it will be lost.
 */

#include "ukui-scale-adaptor.h"
#include <QtCore/QMetaObject>
#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>

/*
 * Implementation of adaptor class ScaleAdaptor
 */

ScaleAdaptor::ScaleAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
    // constructor
    setAutoRelaySignals(true);
}

ScaleAdaptor::~ScaleAdaptor()
{
    // destructor
}

int ScaleAdaptor::setNumScreenMode(const QStringList &stringList)
{
    // handle method call org.ukui.SettingsDaemon.Scale.setNumScreenMode
    int out0;
    QMetaObject::invokeMethod(parent(), "setNumScreenMode", Q_RETURN_ARG(int, out0), Q_ARG(QStringList, stringList));
    return out0;
}

int ScaleAdaptor::setScreenMode(const QString &screenName, const QRect &geometry, bool primary, int rotation, double scale)
{
    // handle method call org.ukui.SettingsDaemon.Scale.setScreenMode
    int out0;
    QMetaObject::invokeMethod(parent(), "setScreenMode", Q_RETURN_ARG(int, out0), Q_ARG(QString, screenName), Q_ARG(QRect, geometry), Q_ARG(bool, primary), Q_ARG(int, rotation), Q_ARG(double, scale));
    return out0;
}

