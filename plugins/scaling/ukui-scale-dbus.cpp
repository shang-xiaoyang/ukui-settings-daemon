#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QtMath>
#include "ukui-scale-dbus.h"
#include "ukui-scale-manager.h"

UkuiScaleManager *scaleManager = nullptr;

UkuiScaleDbus::UkuiScaleDbus(QObject *parent) : QObject(parent)
{
    scaleManager = static_cast<UkuiScaleManager*>(parent);
}

UkuiScaleDbus::~UkuiScaleDbus()
{

}

int UkuiScaleDbus::setScreenMode(QString screenName, QRect geometry, bool primary, int rotation, double scale)
{
    scaleManager->setScreenMode(screenName,geometry,primary,rotation,scale);
    Q_EMIT setScreenModeSignal(screenName);

    return 1;
}

int UkuiScaleDbus::setNumScreenMode(QStringList stringList)
{
    QStringList nameList;
    QList<QMap<QString, QVariant>> mapList;
    QDir dir;
    QString FilePath = dir.homePath() + "/.config/scaling.cfg";
    QFile file;

    file.setFileName(FilePath);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.close();

    for(QString maps : stringList){
        QStringList screenDate, geometryDate;
        QString geometryStr;
        int x,y,w,h;
        double scale;
        QMap<QString, QVariant> screenMap;

        if(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)){
            file.write(maps.toLatin1().data());
            file.write("\n");
        }
        file.close();

        screenDate = maps.split(":");
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
        screenMap.insert("scale", scale);//screenDate.at(4));

        nameList.append(screenMap["screenName"].toString());
        mapList.append(screenMap);
    }
    scaleManager->UkuiScaleManagerSetScreenMapList(mapList);
    Q_EMIT setNumScreenModeSignal(nameList);
    return 1;
}
