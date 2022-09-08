#ifndef MATEOUPUT_H
#define MATEOUPUT_H
#include <QObject>
#include <QDebug>
#include <QList>

#include "clib-syslog.h"


#define DEFINE_QSTRING(var) QString var; QString get##var(){return var;} void set##var(QString e){var = e;}
#define DEFINE_BOOL(var) bool var; bool get##var(){return var;} void set##var(bool e){var = e;}
#define DEFINE_DOUBLE(var) private: double var; public: double get##var(){return var;} void set##var(double e){var = e;}
//#define DEFINE_QSTRING(var) QString get##var(){return var;} void set##var(QString e){e = var;}
typedef struct UsdOutputMode_tag {
    int m_modeId;
    int m_height;
    int m_width;
    double m_rate;
}UsdOutputMode;

class UsdOuputProperty : public QObject{
Q_OBJECT
    Q_PROPERTY(QString name READ getname WRITE setname)
    Q_PROPERTY(QString enable READ getenable WRITE setenable)
    Q_PROPERTY(QString vendor READ getvendor WRITE setvendor)
    Q_PROPERTY(QString product READ getproduct WRITE setproduct)
    Q_PROPERTY(QString serial READ getserial WRITE setserial)
    Q_PROPERTY(QString width READ getwidth WRITE setwidth)
    Q_PROPERTY(QString height READ getheight WRITE setheight)
    Q_PROPERTY(QString rate READ getrate WRITE setrate)
    Q_PROPERTY(QString x READ getx WRITE setx)
    Q_PROPERTY(QString y READ gety WRITE sety)
    Q_PROPERTY(QString rotation READ getrotation WRITE setrotation)
    Q_PROPERTY(QString primary READ getprimary WRITE setprimary)
    Q_PROPERTY(bool seted READ getseted WRITE setseted)
    Q_PROPERTY(double scale READ getscale WRITE setscale)
public:

    UsdOuputProperty(){
//        qDebug()<<"**********************************88" <<"new";
    }

    ~UsdOuputProperty(){
//        qDebug()<<"**********************************88" <<"delete";
    }
    void addMode(UsdOutputMode mode) {
        this->m_modes.append(mode);
    }
    void updateCloneMode(UsdOutputMode outputMode){
        for (int kmode = 0; kmode < this->m_cloneModes.count(); kmode ++) {
            if (this->m_cloneModes[kmode].m_height = outputMode.m_height && this->m_cloneModes[kmode].m_width == outputMode.m_width) {
                if (outputMode.m_rate < m_cloneModes[kmode].m_rate) {
                    return;
                } else {
                    this->m_cloneModes[kmode].m_rate = outputMode.m_rate;
                    this->m_cloneModes[kmode].m_modeId = outputMode.m_modeId;
                    return;
                }
            }
        }

        this->m_cloneModes.append(outputMode);
    }
    void showAllElement(){
        USD_LOG_SHOW_PARAMS(name.toLatin1().data());
        USD_LOG_SHOW_PARAMS(width.toLatin1().data());
        USD_LOG_SHOW_PARAMS(height.toLatin1().data());
        USD_LOG_SHOW_PARAMS(x.toLatin1().data());
        USD_LOG_SHOW_PARAMS(y.toLatin1().data());
        USD_LOG_SHOW_PARAMS(rate.toLatin1().data());
        USD_LOG_SHOW_PARAMS(enable.toLatin1().data());
    }
private:
    DEFINE_QSTRING(name)
    DEFINE_QSTRING(vendor)
    DEFINE_QSTRING(product)
    DEFINE_QSTRING(serial)
    DEFINE_QSTRING(width)
    DEFINE_QSTRING(height)
    DEFINE_QSTRING(rate)
    DEFINE_QSTRING(x)
    DEFINE_QSTRING(y)
    DEFINE_QSTRING(rotation)
    DEFINE_QSTRING(primary)
    DEFINE_QSTRING(enable)
    DEFINE_BOOL(seted)
    DEFINE_DOUBLE(scale)
    QList<UsdOutputMode> m_modes;
    QList<UsdOutputMode> m_cloneModes;
};

class OutputsConfig{
public:
    QString m_clone;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    int m_foundInCrtc = 0;
    QString m_dpi;
    QList<UsdOuputProperty*> m_outputList;
};



#endif // MATEOUPUT_H
