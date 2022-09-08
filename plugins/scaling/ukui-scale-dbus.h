#ifndef UKUISCALEDBUS_H
#define UKUISCALEDBUS_H

#include <QObject>
#include <QRect>
#include <QDBusConnection>
#include <QDBusInterface>

#define DBUS_SCALE_NAME "org.ukui.SettingsDaemon"
#define DBUS_SCALE_PATH "/org/ukui/SettingsDaemon"
#define DBUS_SCALE_INTERFACE "org.ukui.SettingsDaemon"

#define USD_SCALE_DBUS_NAME DBUS_SCALE_NAME ".Scale"
#define USD_SCALE_DBUS_PATH DBUS_SCALE_PATH "/Scale"

class UkuiScaleDbus : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", USD_SCALE_DBUS_NAME)
    //Q_DECLARE_METATYPE(QList<QMap<QString, QVariant>>)

public:
    UkuiScaleDbus(QObject *parent = nullptr);
    ~UkuiScaleDbus();

public Q_SLOTS:
    int setScreenMode(QString screenName, QRect geometry, bool primary, int rotation, double scale); //QList<QMap<QString, QVariant>> *map);
    int setNumScreenMode(QStringList stringList);

Q_SIGNALS:
    //供xrandrManager监听
    void setScreenModeSignal(QString screenName);
    void setNumScreenModeSignal(QStringList stringList);
    //QList<QMap<QString, QVariant>> *map;
};

#endif // UKUISCALEDBUS_H
