/* -*- Mode: C++; indent-tabs-mode: nil; tab-width: 4 -*-
 * -*- coding: utf-8 -*-
 *
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
#include <QFile>
#include <QString>
#include <QList>
#include <QDir>
#include <QVariant>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDebug>
#include "xrdb-manager.h"
#include <syslog.h>
#include <QSettings>
#include <QMetaEnum>
#include <QColor>

#define midColor(x,low,high) (((x) > (high)) ? (high): (((x) < (low)) ? (low) : (x)))

ukuiXrdbManager* ukuiXrdbManager::mXrdbManager = nullptr;

ukuiXrdbManager::ukuiXrdbManager():
    m_theme(0),
    m_themeMode(0)
{
    settings = new QGSettings(SCHEMAS);
    if(QGSettings::isSchemaInstalled(UKUI_STYLE_SCHEMAS)) {
        styleSettings = new QGSettings(UKUI_STYLE_SCHEMAS);
    }
    allUsefulAdFiles = new QList<QString>();

    QString filename = QDir::homePath() + QStringLiteral("/.config/ukui-decorationrc");
    QSettings *themeSettings = new QSettings(filename, QSettings::IniFormat);
    m_theme = qvariant_cast<KylinTheme>(themeSettings->value(QStringLiteral("ThemeMode/theme")));
    m_themeMode = qvariant_cast<KylinThemeMode>(themeSettings->value(QStringLiteral("ThemeMode/mode")));
    themeSettings->deleteLater();
    gtk_init(NULL,NULL);
}

ukuiXrdbManager::~ukuiXrdbManager()
{
    if (settings) {
        delete settings;
        settings = nullptr;
    }
    if (allUsefulAdFiles) {
        allUsefulAdFiles->clear();
        delete allUsefulAdFiles;
        allUsefulAdFiles = nullptr;
    }
}

//singleton
ukuiXrdbManager* ukuiXrdbManager::ukuiXrdbManagerNew()
{
    if(nullptr == mXrdbManager)
        mXrdbManager = new ukuiXrdbManager();
    return mXrdbManager;
}

bool ukuiXrdbManager::start(GError **error)
{
    USD_LOG(LOG_DEBUG,"Starting xrdb manager!");

    widget = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    //gtk_widget_realize (widget);
    gtk_widget_ensure_style(widget);
    /* the initialization is done here otherwise
       ukui_settings_xsettings_load would generate
       false hit as gtk-theme-name is set to Default in
       ukui_settings_xsettings_init */
    if(settings) {
        connect(settings,SIGNAL(changed(const QString&)),this,SLOT(themeChanged(const QString&)));
    }

    if(styleSettings) {
        connect(styleSettings,SIGNAL(changed(const QString&)),this,SLOT(themeChanged(const QString&)));
    }

    return true;
}

void ukuiXrdbManager::stop()
{
    USD_LOG(LOG_DEBUG,"Stopping xrdb manager!");

    //destroy newed GtkWidget window
    gtk_widget_destroy(widget);
}

/** func : Scan .ad file from @path, and return them all in a QList
 *         从@path目录下查找 .ad 文件，并存储在 QList。
 */
QList<QString>* scanAdDirectory(QString path,GError** error)
{
    QFileInfoList fileList;
    QString tmpFileName;
    QList<QString>* tmpFileList;
    QDir dir;
    int fileCount;
    int i = 0;

    dir.setPath(path);
    if(!dir.exists()){
        g_set_error(error,
                    G_FILE_ERROR,
                    G_FILE_ERROR_EXIST,
                    "%s does not exist!",
                    path.toLatin1().data());
        return nullptr;
    }

    fileList = dir.entryInfoList();
    fileCount = fileList.count();
    tmpFileList = new QList<QString>();
    for(;i < fileCount; ++i){
        tmpFileName =  fileList.at(i).absoluteFilePath();
        if(tmpFileName.contains(".ad"))
            tmpFileList->push_back(tmpFileName);
    }

    if(tmpFileList->size() > 0)
        tmpFileList->sort();

    return tmpFileList;
}

/**
 * @brief ukuiXrdbManager::scanForFiles
 *        scan .ad file from @SYSTEM_AD_DIR and $HOME, and return them at a QList;
 *        从@SYSTEM_AD_DIR 和 $HOME扫描 .ad 文件，将结果存储在QList
 * @param error
 * @return
 */
QList<QString>* ukuiXrdbManager::scanForFiles(GError** error)
{
    QString userHomeDir;
    QList<QString>* userAdFileList;
    QList<QString>* systemAdFileList;//remeber free
    GError* localError;

    systemAdFileList = userAdFileList = nullptr;

    //look for system ad files at /etc/xrdb/
    localError = NULL;
    systemAdFileList = scanAdDirectory(SYSTEM_AD_DIR,&localError);
    if(NULL != localError){
        g_propagate_error(error,localError);        //copy error info
        return nullptr;
    }

    //look for ad files at user's home.
    userHomeDir = QDir::homePath();
    if(!userHomeDir.isEmpty()){
        QString userAdDir;
        QFileInfo fileInfo;

        userAdDir = userHomeDir + "/" + USER_AD_DIR;
        fileInfo.setFile(userAdDir);
        if(fileInfo.exists() && fileInfo.isDir()){
            userAdFileList = scanAdDirectory(userAdDir,&localError);
            //what remians here is open source logic 这里保留的是开源的逻辑
            if(NULL != localError){
                g_propagate_error(error,localError);    //copy error info
                systemAdFileList->clear();              //memery free for QList
                delete systemAdFileList;
                return nullptr;
            }
        }else
            USD_LOG(LOG_INFO,"User's ad file not found at %s!",userAdDir.toLatin1().data());
    }else
        USD_LOG(LOG_WARNING,"Cannot datermine user's home directory!");

    //After get all ad files,we handle it. 在得到所有的ad文件后，我们开始处理数据
    if(systemAdFileList->contains(GENERAL_AD))
        systemAdFileList->removeOne(GENERAL_AD);
    if(nullptr != userAdFileList)
        removeSameItemFromFirst(systemAdFileList,userAdFileList);

    //here,we get all ad files that we needed(without repetition). 到这里，取得我们所有需要的 ad 文件(不重复)
    allUsefulAdFiles->append(*systemAdFileList);
    if(nullptr != userAdFileList)
        allUsefulAdFiles->append(*userAdFileList);
    allUsefulAdFiles->append(GENERAL_AD);

    //QList.append() operator is deep-copy,so we free memory. QList.append()会进行深拷贝，所以这里释放内存
    systemAdFileList->clear();
    delete systemAdFileList;

    if(nullptr != userAdFileList){
        userAdFileList->clear();
        delete userAdFileList;
    }

    return allUsefulAdFiles;
}

/**
 * Append the contents of @file onto the end of a QString
 * 追加 @file 文件的内容到QString尾部
 */
void ukuiXrdbManager::appendFile(QString file,GError** error)
{
    GError* localError;
    QString fileContents;

    //first get all contents from file.
    localError = NULL;
    fileContents =  fileGetContents(file,&localError);

    if(NULL != localError){
        g_propagate_error(error,localError);    //copy error info
        localError = NULL;
        return;
    }

    //then append all contents to @needMerge
    if(!fileContents.isNull())
        needMerge.append(fileContents);
}

/** func : append contents from .Xresources or .Xdefaults  to @needMerge.
 */
void ukuiXrdbManager::appendXresourceFile(QString fileName,GError **error)
{
    QString homePath;
    QString xResources;
    QFile file;
    GError* localError;
    char* tmpName;

    homePath = QDir::homePath();
    xResources = homePath + "/" + fileName;
    file.setFileName(xResources);

    if(!file.exists()){
        tmpName = xResources.toLatin1().data();
        g_set_error(error,G_FILE_ERROR,
                    G_FILE_ERROR_NOENT,
                    "%s does not exist!",tmpName);
        //USD_LOG(LOG_WARNING,"%s does not exist!",tmpName);
        return;
    }

    localError = NULL;
    appendFile(xResources,&localError);
    if(NULL != localError){
        g_propagate_error(error,localError);
        //USD_LOG(LOG_WARNING,"%s",localError->message);
        localError = NULL;
    }
}

bool
write_all (int fd,const char *buf,ulong to_write)
{
    while (to_write > 0) {
        long count = write (fd, buf, to_write);
        if (count < 0) {
            return false;
        } else {
            to_write -= count;
            buf += count;
        }
    }

    return true;
}


void
child_watch_cb (int     pid,
                int      status,
                gpointer user_data)
{
    char *command = (char*)user_data;

    if (!WIFEXITED (status) || WEXITSTATUS (status)) {
        USD_LOG(LOG_WARNING,"Command %s failed", command);
    }
}


void
spawn_with_input (const char *command,
                  const char *input)
{
    char   **argv;
    int      child_pid;
    int      inpipe;
    GError  *error;
    bool res;

    argv = NULL;
    res = g_shell_parse_argv (command, NULL, &argv, NULL);
    if (! res) {
        USD_LOG(LOG_WARNING,"Unable to parse command: %s", command);
        return;
    }

    error = NULL;
    res = g_spawn_async_with_pipes (NULL,
                                    argv,
                                    NULL,
                                    (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
                                    NULL,
                                    NULL,
                                    &child_pid,
                                    &inpipe,
                                    NULL,
                                    NULL,
                                    &error);
    g_strfreev (argv);

    if (! res) {
        USD_LOG(LOG_WARNING,"Could not execute %s: %s", command, error->message);
        g_error_free (error);
        return;
    }

    if (input != NULL) {
        if (! write_all (inpipe, input, strlen (input))) {
            USD_LOG(LOG_WARNING,"Could not write input to %s", command);
        }
        close (inpipe);
    }

    g_child_watch_add (child_pid, (GChildWatchFunc) child_watch_cb, (gpointer)command);

}

/* 1 according to the current theme,get color value.
 * 2 get contents from ad files.
 * 3 exec command: "xrdb -merge -quiet ....".
 */
void ukuiXrdbManager::applySettings(){
    char* command;
    GError* error;
    int i,fileNum;
    int listCount;

    command = "xrdb -merge -quiet";

    if(!colorDefineList.isEmpty()){
        listCount = colorDefineList.count();
        for( i = 0; i < listCount; ++i)
            needMerge.append(colorDefineList.at(i));
        colorDefineList.clear();
    }

    //first, get system ad files and user's ad files
    error = NULL;
    scanForFiles(&error);
    if(NULL != error){
        USD_LOG(LOG_WARNING,"%s",error->message);
        g_error_free(error);
    }

    //second, get contents from every file,and append contends to @needMerge.
    fileNum = allUsefulAdFiles->count();
    for(i = 0; i < fileNum; ++i){
        error = NULL;
        appendFile(allUsefulAdFiles->at(i),&error);
        if(NULL != error){
            USD_LOG(LOG_WARNING,"%s",error->message);
            g_error_free(error);
        }
    }

    //third, append Xresources file's contents to @needMerge.
    error = NULL;
    appendXresourceFile(USER_X_RESOURCES,&error);
    if(NULL != error){
        USD_LOG(LOG_WARNING,"%s",error->message);
        g_error_free(error);
    }

    error = NULL;
    appendXresourceFile(USER_X_DEFAULTS,&error);
    if(NULL != error){
        USD_LOG(LOG_WARNING,"%s",error->message);
        g_error_free(error);
    }

    //last, exec shell: @command + @needMerge
    spawn_with_input(command,needMerge.toLatin1().data());

    needMerge.clear();
    allUsefulAdFiles->clear();
}

/** func : private slots for gsettings key 'gtk-theme' changed
 *         监听 'gtk-theme' key值变化的槽函数
 */
void ukuiXrdbManager::themeChanged (const QString& key)
{
    /* 监听主题更改，发送dbus信号 */
    if(key.compare("gtk-theme")==0){

        QString value = settings->get(key).toString();
        USD_LOG(LOG_DEBUG,"key:%s value:%s", key.toLatin1().data(), value.toLatin1().data());
        if (m_whiteThemeNameList.contains(value)) {
            m_themeMode = Light;
        } else if (m_blackThemeNameList.contains(value)) {
            m_themeMode = Dark;
        }
    }
    if(key == QStringLiteral("widget-theme-name")) {
        QString value = styleSettings->get(key).toString();
        if(value == QStringLiteral("default")) {
            m_theme = ThemeDefault;
        } else if(value == QStringLiteral("fashion")) {
            m_theme = ThemeFashion;
        } else if(value == QStringLiteral("classical")) {
            m_theme = ThemeClassical;
        }
    }
    saveThemeConfig();
    QDBusMessage message =
    QDBusMessage::createSignal("/KGlobalSettings",
                                       "org.kde.KGlobalSettings",
                                       "slotThemeChange");
    message << m_themeMode;
    QDBusConnection::sessionBus().send(message);
//    getColorConfigFromGtkWindow();
    //applySettings();
}

QStringList colorToString(const QColor& color)
{
    QStringList strList;
    strList<<QString::number(color.red())
           <<QString::number(color.green())
           <<QString::number(color.blue());
    return strList;
}

void ukuiXrdbManager::saveThemeConfig ()
{
    QString filename = QDir::homePath() + QStringLiteral("/.config/ukui-decorationrc");
    QSettings *themeSettings = new QSettings(filename, QSettings::IniFormat);
    themeSettings->setValue(QStringLiteral("ThemeMode/theme"),m_theme);
    themeSettings->setValue(QStringLiteral("ThemeMode/mode"),m_themeMode);
    themeSettings->beginGroup(QStringLiteral("Theme"));
    if(m_theme == ThemeDefault) {
        if(m_themeMode == Dark) {
            themeSettings->setValue(QStringLiteral("radius"),12);
            themeSettings->setValue(QStringLiteral("shadow_border"),30);
            themeSettings->setValue(QStringLiteral("shadowcolor_alphaf"),0.15);
            themeSettings->setValue(QStringLiteral("active_darkness"),0.5);

            themeSettings->setValue(QStringLiteral("frame_active_color"),colorToString(QColor(18,18,18)));
            themeSettings->setValue(QStringLiteral("frame_inactive_color"),colorToString(QColor(28,28,28)));

            themeSettings->setValue(QStringLiteral("border_active_color"),colorToString(QColor(233,231,230)));
            themeSettings->setValue(QStringLiteral("border_inactive_color"),colorToString(QColor(233,231,230)));
        } else {
            themeSettings->setValue(QStringLiteral("radius"),12);
            themeSettings->setValue(QStringLiteral("shadow_border"),30);
            themeSettings->setValue(QStringLiteral("shadowcolor_alphaf"),0.15);
            themeSettings->setValue(QStringLiteral("active_darkness"),0.5);
            themeSettings->setValue(QStringLiteral("frame_active_color"),colorToString(QColor(255,255,255)));
            themeSettings->setValue(QStringLiteral("frame_inactive_color"),colorToString(QColor(245,245,245)));
            themeSettings->setValue(QStringLiteral("border_active_color"),colorToString(QColor(197,194,192)));
            themeSettings->setValue(QStringLiteral("border_inactive_color"),colorToString(QColor(197,194,192)));
        }

    } else if(m_theme == ThemeFashion) {
        if(m_themeMode == Dark) {
            themeSettings->setValue(QStringLiteral("radius"),12);
            themeSettings->setValue(QStringLiteral("shadow_border"),30);
            themeSettings->setValue(QStringLiteral("shadowcolor_alphaf"),0.15);
            themeSettings->setValue(QStringLiteral("active_darkness"),0.5);

            themeSettings->setValue(QStringLiteral("frame_active_color"),colorToString(QColor(18,18,18)));
            themeSettings->setValue(QStringLiteral("frame_inactive_color"),colorToString(QColor(28,28,28)));

            themeSettings->setValue(QStringLiteral("border_active_color"),colorToString(QColor(233,231,230)));
            themeSettings->setValue(QStringLiteral("border_inactive_color"),colorToString(QColor(233,231,230)));
        } else {
            themeSettings->setValue(QStringLiteral("radius"),12);
            themeSettings->setValue(QStringLiteral("shadow_border"),30);
            themeSettings->setValue(QStringLiteral("shadowcolor_alphaf"),0.15);
            themeSettings->setValue(QStringLiteral("active_darkness"),0.5);
            themeSettings->setValue(QStringLiteral("frame_active_color"),colorToString(QColor(255,255,255)));
            themeSettings->setValue(QStringLiteral("frame_inactive_color"),colorToString(QColor(245,245,245)));
            themeSettings->setValue(QStringLiteral("border_active_color"),colorToString(QColor(197,194,192)));
            themeSettings->setValue(QStringLiteral("border_inactive_color"),colorToString(QColor(197,194,192)));
        }

    } else if(m_theme == ThemeClassical) {
        if(m_themeMode == Dark) {
            themeSettings->setValue(QStringLiteral("radius"),0);
            themeSettings->setValue(QStringLiteral("shadow_border"),1);
            themeSettings->setValue(QStringLiteral("shadowcolor_alphaf"),1.0);
            themeSettings->setValue(QStringLiteral("active_darkness"),0);

            themeSettings->setValue(QStringLiteral("frame_active_color"),colorToString(QColor(43,121,218)));
            themeSettings->setValue(QStringLiteral("frame_inactive_color"),colorToString(QColor(34,34,34)));
            themeSettings->setValue(QStringLiteral("border_active_color"), colorToString(QColor(21,87,113)));
            themeSettings->setValue(QStringLiteral("border_inactive_color"),colorToString(QColor(89,89,89)));

        } else {
            themeSettings->setValue(QStringLiteral("radius"),0);
            themeSettings->setValue(QStringLiteral("shadow_border"),1);
            themeSettings->setValue(QStringLiteral("shadowcolor_alphaf"),1.0);
            themeSettings->setValue(QStringLiteral("active_darkness"),0);
            themeSettings->setValue(QStringLiteral("frame_active_color"),colorToString(QColor(43,121,218)));
            themeSettings->setValue(QStringLiteral("frame_inactive_color"),colorToString(QColor(237,237,237)));
            themeSettings->setValue(QStringLiteral("border_active_color"), colorToString(QColor(21,87,113)));
            themeSettings->setValue(QStringLiteral("border_inactive_color"),colorToString(QColor(187,187,187)));
        }
    } else {
        if(m_themeMode == Dark) {
            themeSettings->setValue(QStringLiteral("radius"),12);
            themeSettings->setValue(QStringLiteral("shadow_border"),30);
            themeSettings->setValue(QStringLiteral("shadowcolor_alphaf"),0.15);
            themeSettings->setValue(QStringLiteral("active_darkness"),0.5);

            themeSettings->setValue(QStringLiteral("frame_active_color"),colorToString(QColor(18,18,18)));
            themeSettings->setValue(QStringLiteral("frame_inactive_color"),colorToString(QColor(28,28,28)));

            themeSettings->setValue(QStringLiteral("border_active_color"),colorToString(QColor(233,231,230)));
            themeSettings->setValue(QStringLiteral("border_inactive_color"),colorToString(QColor(233,231,230)));
        } else {
            themeSettings->setValue(QStringLiteral("radius"),12);
            themeSettings->setValue(QStringLiteral("shadow_border"),30);
            themeSettings->setValue(QStringLiteral("shadowcolor_alphaf"),0.15);
            themeSettings->setValue(QStringLiteral("active_darkness"),0.5);
            themeSettings->setValue(QStringLiteral("frame_active_color"),colorToString(QColor(255,255,255)));
            themeSettings->setValue(QStringLiteral("frame_inactive_color"),colorToString(QColor(245,245,245)));
            themeSettings->setValue(QStringLiteral("border_active_color"),colorToString(QColor(197,194,192)));
            themeSettings->setValue(QStringLiteral("border_inactive_color"),colorToString(QColor(197,194,192)));
        }
    }


    themeSettings->endGroup();
    themeSettings->sync();
    themeSettings->deleteLater();
}


/* func : remove one item from first,if second have this item too.
 *        the point is : name is same.
 * example: first.at(3) == "/etc/xrdb/tmp1.ad"
 *          second.at(1) == $QDir::homePath + "/.config/ukui/tmp1.ad"
 *          then exec : first.removeAt(3);
 */
void ukuiXrdbManager::removeSameItemFromFirst(QList<QString>* first,
                             QList<QString>* second){
    QFileInfo tmpFirstName;
    QFileInfo tmpSecondName;
    QString firstBaseName;      //real file name,not include path 不含路径的真实文件名
    QString secondBaseName;     //same as above 同上
    int i,j;
    int firstSize,secondSize;
//    if(first->isEmpty() || second->isEmpty()){
//        return;
//    }

    first->length();
    firstSize = first->size();
    secondSize = second->size();

    for(i = 0; i < firstSize; ++i){
        firstBaseName.clear();
        tmpFirstName.setFile(first->at(i));
        firstBaseName = tmpFirstName.fileName();

        for(j = 0; j < secondSize; ++j){
            secondBaseName.clear();
            tmpSecondName.setFile(second->at(j));
            secondBaseName = tmpSecondName.fileName();
            if(firstBaseName == secondBaseName){
                first->removeAt(i);
                firstSize -= 1;
                break;
            }
        }
    }
}

/** func : read all contents from @fileName.
 *         从文件 @fileName 读取全部内容
 *  @return
 *      return nullptr if open failed or @fileName does not exist
 *      若文件 @fileName 不存在或者对打开失败，则返回 nullptr
 */
QString ukuiXrdbManager::fileGetContents(QString fileName,GError **error)
{
    QFile file;
    QString fileContents;

    file.setFileName(fileName);
    if(!file.exists()){
        g_set_error(error,G_FILE_ERROR,
                    G_FILE_ERROR_NOENT,
                    "%s does not exists!",fileName.toLatin1().data());
        return nullptr;
    }

    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        g_set_error(error,G_FILE_ERROR,
                    G_FILE_ERROR_FAILED,
                    "%s open failed!",fileName.toLatin1().data());
        return nullptr;
    }

    fileContents = file.readAll();

    return fileContents;
}
/**
 * @brief ukuiXrdbManager::getColorConfigFromGtkWindow
 *  gets the color configuration for the gtk window.
 *  获取gtk窗体的颜色配置信息
 */
void ukuiXrdbManager::getColorConfigFromGtkWindow()
{
    GtkStyle* style;
    style = gtk_widget_get_style(widget);

    appendColor("BACKGROUND",&style->bg[GTK_STATE_NORMAL]);
    appendColor("FOREGROUND",&style->fg[GTK_STATE_NORMAL]);
    appendColor("SELECT_BACKGROUND",&style->bg[GTK_STATE_SELECTED]);
    appendColor("SELECT_FOREGROUND",&style->text[GTK_STATE_SELECTED]);
    appendColor("WINDOW_BACKGROUND",&style->base[GTK_STATE_NORMAL]);
    appendColor("WINDOW_FOREGROUND",&style->text[GTK_STATE_NORMAL]);
    appendColor("INACTIVE_BACKGROUND",&style->bg[GTK_STATE_INSENSITIVE]);
    appendColor("INACTIVE_FOREGROUND",&style->text[GTK_STATE_INSENSITIVE]);
    appendColor("ACTIVE_BACKGROUND",&style->bg[GTK_STATE_SELECTED]);
    appendColor("ACTIVE_FOREGROUND",&style->text[GTK_STATE_SELECTED]);

    colorShade("HIGHLIGHT",&style->bg[GTK_STATE_NORMAL],1.2);
    colorShade("LOWLIGHT",&style->fg[GTK_STATE_NORMAL],2.0/3.0);

}

/* func : Gets the hexadecimal value of the color
 *        获取颜色值的16进制表示
 * example : #define BACKGROUND #ffffff\n
 */
void ukuiXrdbManager::appendColor(QString name,GdkColor* color)
{
    QString tmp;
    tmp = QString("#%1%2%3\n").arg(color->red>>8,2,16,QLatin1Char('0'))
                .arg(color->green>>8,2,16,QLatin1Char('0')).arg(color->blue>>8,2,16,QLatin1Char('0'));
    colorDefineList.append("#define " + name + " " + tmp);
}

void ukuiXrdbManager::colorShade(QString name,GdkColor* color,double shade)
{
    GdkColor tmp;

    tmp.red = midColor((color->red)*shade,0,0xFFFF);
    tmp.green = midColor((color->green)*shade,0,0xFFFF);
    tmp.blue = midColor((color->blue)*shade,0,0xFFFF);

    appendColor(name,&tmp);
}
