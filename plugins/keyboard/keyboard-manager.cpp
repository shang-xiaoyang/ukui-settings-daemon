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
#include "keyboard-manager.h"
#include "clib-syslog.h"
#include "config.h"
#include "rfkillswitch.h"

#define USD_KEYBOARD_SCHEMA  "org.ukui.peripherals-keyboard"

#define UKCCOSD_SCHEMA "org.ukui.control-center.osd"
#define KYCCOSD_SCHEMA "org.kylin.control-center.osd"
#define KDSOSD_SCHEMA "org.ukui.kds.osd"

#define SHOW_TIP_KEY "show-lock-tip"

#define KEY_REPEAT           "repeat"
#define KEY_CLICK            "click"
#define KEY_RATE             "rate"
#define KEY_DELAY            "delay"
#define KEY_CLICK_VOLUME     "click-volume"
#define KEY_BELL_PITCH       "bell-pitch"
#define KEY_BELL_DURATION    "bell-duration"
#define KEY_BELL_MODE        "bell-mode"
#define KEY_NUMLOCK_STATE    "numlock-state"
#define KEY_CAPSLOCK_STATE   "capslock-state"
#define KEY_NUMLOCK_REMEMBER "remember-numlock-state"

typedef enum {
        NUMLOCK_STATE_OFF = 0,
        NUMLOCK_STATE_ON  = 1,
        NUMLOCK_STATE_UNKNOWN = 2
} NumLockState;

static void numlock_set_xkb_state (NumLockState new_state);
static void capslock_set_xkb_state(gboolean lock_state);

KeyboardManager *KeyboardManager::mKeyboardManager = nullptr;
KeyboardXkb     *KeyboardManager::mKeyXkb = nullptr;

KeyboardManager::KeyboardManager(QObject * parent)
{
    if (mKeyXkb == nullptr)
        mKeyXkb = new KeyboardXkb;

    settings = new QGSettings(USD_KEYBOARD_SCHEMA);

    stInstalled = true;
    time = new QTimer(this);

    const QByteArray id(UKCCOSD_SCHEMA);
    const QByteArray idd(KYCCOSD_SCHEMA);
    const QByteArray iid(KDSOSD_SCHEMA);

    ifaceScreenSaver = new QDBusInterface("org.ukui.ScreenSaver", \
                                          "/", \
                                          "org.ukui.ScreenSaver", \
                                          QDBusConnection::sessionBus());


    if (QGSettings::isSchemaInstalled(id)){
        ksettings = new QGSettings(id);
    } else if (QGSettings::isSchemaInstalled(idd)){
        ksettings = new QGSettings(idd);
    } else if (QGSettings::isSchemaInstalled(iid)){
        ksettings = new QGSettings(iid);
    } else {
        stInstalled = false;
    }
    m_statusWidget = new KeyboardWidget();
}

KeyboardManager::~KeyboardManager()
{
    if (mKeyXkb) {
        delete mKeyXkb;
        mKeyXkb = nullptr;
    }
    if (settings) {
        delete settings;
        settings = nullptr;
    }
    if (time) {
        delete time;
        time = nullptr;
    }
    if (m_statusWidget) {
        delete m_statusWidget;
        m_statusWidget = nullptr;
    }
    if (ksettings) {
        delete ksettings;
        ksettings = nullptr;
    }
}

KeyboardManager *KeyboardManager::KeyboardManagerNew()
{
    if (nullptr == mKeyboardManager)
        mKeyboardManager = new KeyboardManager(nullptr);
    return  mKeyboardManager;
}


bool KeyboardManager::KeyboardManagerStart()
{
    USD_LOG(LOG_DEBUG,"-- Keyboard Start Manager --");

    connect(time,SIGNAL(timeout()),this,SLOT(start_keyboard_idle_cb()));
    time->start(1500);

    return true;
}

void KeyboardManager::KeyboardManagerStop()
{
    USD_LOG(LOG_DEBUG,"-- Keyboard Stop Manager --");

    old_state = 0;
    numlock_set_xkb_state((NumLockState)old_state);
    capslock_set_xkb_state(FALSE);

    mKeyXkb->usd_keyboard_xkb_shutdown ();
}


#ifdef HAVE_X11_EXTENSIONS_XF86MISC_H
static gboolean xfree86_set_keyboard_autorepeat_rate(int delay, int rate)
{
    gboolean res = FALSE;
    int      event_base_return;
    int      error_base_return;

    if (XF86MiscQueryExtension (GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                                &event_base_return,
                                &error_base_return) == True) {
            /* load the current settings */
            XF86MiscKbdSettings kbdsettings;
            XF86MiscGetKbdSettings (GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), &kbdsettings);

            /* assign the new values */
            kbdsettings.delay = delay;
            kbdsettings.rate = rate;
            XF86MiscSetKbdSettings (GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), &kbdsettings);
            res = TRUE;
    }

    return res;
}
#endif /* HAVE_X11_EXTENSIONS_XF86MISC_H */

void numlock_xkb_init (KeyboardManager *manager)
{
    Display *dpy = QX11Info::display();
    gboolean have_xkb;
    int opcode, error_base, major, minor;

    have_xkb = XkbQueryExtension (dpy,
                                  &opcode,
                                  &manager->xkb_event_base,
                                  &error_base,
                                  &major,
                                  &minor)
            && XkbUseExtension (dpy, &major, &minor);

    if (have_xkb) {
        XkbSelectEventDetails (dpy,
                               XkbUseCoreKbd,
                               XkbStateNotifyMask,
                               XkbModifierLockMask,
                               XkbModifierLockMask);
    } else {
        qWarning ("XKB extension not available");
    }

    manager->have_xkb = have_xkb;
}

static NumLockState numlock_get_settings_state (QGSettings *settings)
{
    int  curr_state;
    curr_state = settings->getEnum(KEY_NUMLOCK_STATE);
    return (NumLockState)curr_state;
}

static void capslock_set_xkb_state(gboolean lock_state)
{
    unsigned int caps_mask;
    Display *dpy = QX11Info::display();
    caps_mask = XkbKeysymToModifiers (dpy, XK_Caps_Lock);
    XkbLockModifiers (dpy, XkbUseCoreKbd, caps_mask, lock_state ? caps_mask : 0);
    XSync (dpy, FALSE);
}

static unsigned numlock_NumLock_modifier_mask (void)
{
    Display *dpy = QX11Info::display();
    return XkbKeysymToModifiers (dpy, XK_Num_Lock);
}

static void numlock_set_xkb_state (NumLockState new_state)
{
    unsigned int num_mask;
    Display *dpy = QX11Info::display();
    if (new_state != NUMLOCK_STATE_ON && new_state != NUMLOCK_STATE_OFF)
            return;
    num_mask = numlock_NumLock_modifier_mask ();
    XkbLockModifiers (dpy, XkbUseCoreKbd, num_mask, new_state ? num_mask : 0);
}

void apply_bell (KeyboardManager *manager)
{
    QGSettings       *settings;
    XKeyboardControl kbdcontrol;
    bool             click;
    int              bell_volume;
    int              bell_pitch;
    int              bell_duration;
    char            *volume_string;
    QString          volume_strings;
    int              click_volume;
    Display *dpy = QX11Info::display();

    settings      = manager->settings;
    click         = settings->get(KEY_CLICK).toBool();
    click_volume  = settings->get(KEY_CLICK_VOLUME).toInt();
    bell_pitch    = settings->get(KEY_BELL_PITCH).toInt();
    bell_duration = settings->get(KEY_BELL_DURATION).toInt();

    volume_strings = settings->get(KEY_BELL_MODE).toChar();
    volume_string = volume_strings.toLatin1().data();
    bell_volume   = (volume_string && !strcmp (volume_string, "on")) ? 50 : 0;

    /* as percentage from 0..100 inclusive */
    if (click_volume < 0) {
        click_volume = 0;
    } else if (click_volume > 100) {
        click_volume = 100;
    }
    kbdcontrol.key_click_percent = click ? click_volume : 0;
    kbdcontrol.bell_percent = bell_volume;
    kbdcontrol.bell_pitch = bell_pitch;
    kbdcontrol.bell_duration = bell_duration;
    try {
        XChangeKeyboardControl (dpy,KBKeyClickPercent |
                                KBBellPercent | KBBellPitch |
                                KBBellDuration, &kbdcontrol);
        XSync (dpy, FALSE);
    } catch (int x) {

    }

}

void apply_numlock (KeyboardManager *manager)
{
    QGSettings *settings;
    bool rnumlock;
    Display *dpy = QX11Info::display();

    qDebug ("Applying the num-lock settings");
    settings = manager->settings;
    rnumlock = settings->get(KEY_NUMLOCK_REMEMBER).toBool();
    manager->old_state = settings->getEnum(KEY_NUMLOCK_STATE);

    try {
        if(true == UsdBaseClass::isTablet() && rnumlock) {
            numlock_set_xkb_state (NUMLOCK_STATE_OFF);
        } else {
            numlock_set_xkb_state ((NumLockState)manager->old_state);
        }
        XSync (dpy, FALSE);
    } catch (int x) {

    }
}

static gboolean xkb_set_keyboard_autorepeat_rate(int delay, int rate)
{
    int interval = (rate <= 0) ? 1000000 : 1000/rate;
    Display *dpy = QX11Info::display();
    if (delay <= 0)
    {
        delay = 1;
    }
    return XkbSetAutoRepeatRate(dpy, XkbUseCoreKbd, delay, interval);
}

void apply_repeat (KeyboardManager *manager)
{
    bool    repeat;
    int     rate;
    int     delay;
    Display *dpy = QX11Info::display();

    repeat  = manager->settings->get(KEY_REPEAT).toBool();
    rate    = manager->settings->get(KEY_RATE).toInt();
    delay   = manager->settings->get(KEY_DELAY).toInt();

    try {
        if (repeat) {
            gboolean rate_set = FALSE;

            XAutoRepeatOn (dpy);
            /* Use XKB in preference */
            rate_set = xkb_set_keyboard_autorepeat_rate (delay, rate);
            if (!rate_set) {
                    USD_LOG(LOG_DEBUG,"Neither XKeyboard not Xfree86's keyboard extensions are available,\n"
                               "no way to support keyboard autorepeat rate settings");
            }
        } else {
            XAutoRepeatOff (dpy);
        }

        XSync (dpy, FALSE);
    } catch (int x) {
        USD_LOG(LOG_ERR,"ERROR");
    }
}



void KeyboardManager::apply_settings (QString keys)
{
    /**
     * Fix by HB* system reboot but rnumlock not available;
    **/

    char *key;
    if(keys != NULL)
        key = keys.toLatin1().data();
    else
        key=NULL;

#ifdef HAVE_X11_EXTENSIONS_XKB_H
    bool rnumlock;
    rnumlock = settings->get(KEY_NUMLOCK_REMEMBER).toBool();

    if (rnumlock == 0 || key == NULL) {
        if (have_xkb && rnumlock) {
            numlock_set_xkb_state (numlock_get_settings_state (settings));
            capslock_set_xkb_state(settings->get(KEY_CAPSLOCK_STATE).toBool());
            USD_LOG(LOG_DEBUG,"apply keyboard ok.");
        }
    }

#endif /* HAVE_X11_EXTENSIONS_XKB_H */

    if (keys.compare(QString::fromLocal8Bit(KEY_CLICK)) == 0||
        keys.compare(QString::fromLocal8Bit(KEY_CLICK_VOLUME)) == 0 ||
        keys.compare(QString::fromLocal8Bit(KEY_BELL_PITCH)) == 0 ||
        keys.compare(QString::fromLocal8Bit(KEY_BELL_DURATION)) == 0 ||
        keys.compare(QString::fromLocal8Bit(KEY_BELL_MODE)) == 0) {
                USD_LOG(LOG_DEBUG,"Bell setting '%s' changed, applying bell settings", key);
                apply_bell (this);

    } else if (keys.compare(QString::fromLocal8Bit(KEY_NUMLOCK_REMEMBER)) == 0) {
             USD_LOG(LOG_DEBUG,"Remember Num-Lock state '%s' changed, applying num-lock settings", key);
            apply_numlock (this);

    } else if (keys.compare(QString::fromLocal8Bit(KEY_NUMLOCK_STATE)) == 0) {
             USD_LOG(LOG_DEBUG,"Num-Lock state '%s' changed, will apply at next startup", key);

    } else if (keys.compare(QString::fromLocal8Bit(KEY_REPEAT)) == 0 ||
               keys.compare(QString::fromLocal8Bit(KEY_RATE)) == 0 ||
               keys.compare(QString::fromLocal8Bit(KEY_DELAY)) == 0) {
             USD_LOG(LOG_DEBUG,"Key repeat setting '%s' changed, applying key repeat settings", key);
            apply_repeat (this);

    } else {
//             USD_LOG(LOG_DEBUG,"Unhandled settings change, key '%s'", key);
    }

}

void KeyboardManager::usd_keyboard_manager_apply_settings (KeyboardManager *manager)
{
     apply_settings(NULL);
}

void KeyboardManager::XkbEventsFilter(int keyCode)
{
    NumLockState numlockState;
    bool capsLockState;

    if (keyCode != 77 && keyCode != 66) {
        return ;
    }

    if (isCloudDesktopTop()) {
        return;
    }
    Display *display = XOpenDisplay(NULL);
    if(keyCode == 77)
    {
        unsigned int numLockedMods;

        XkbGetIndicatorState(display, XkbUseCoreKbd, &numLockedMods);


        if(numLockedMods == 2 || numLockedMods==3)
        {
            numlockState = NUMLOCK_STATE_ON;
        }
        else
        {
            numlockState = NUMLOCK_STATE_OFF;
        }
        USD_LOG(LOG_ERR,"old_state=%d,locked_mods=%d,numlockState=%d",
                  old_state,numLockedMods,numlockState);

        if (numlockState != old_state) {
                settings->setEnum(KEY_NUMLOCK_STATE, numlockState);
                old_state = numlockState;
        }

        if (stInstalled && !ksettings->get(SHOW_TIP_KEY).toBool()){
            USD_LOG(LOG_DEBUG,"MediaKey Tip is Closed\n");
            XCloseDisplay (display);
            return;
        }


        if (ifaceScreenSaver->isValid()){
            QDBusReply<bool>reply = ifaceScreenSaver->call("GetLockState");
            if (reply.isValid()){
                if (reply.value()){
                    qWarning("MediaKey Tip is Closed because ScreenLock\n");
                    XCloseDisplay (display);
                    return;
                }
            }
        }
        if(numlockState)
        {
            m_statusWidget->setIcons("ukui-numlock-on");
            m_statusWidget->showWidget();
        }
        else
        {
            m_statusWidget->setIcons("ukui-numlock-off");
            m_statusWidget->showWidget();
        }

    }else if(keyCode == 66)
    {
        unsigned int capsLockedMods;
        XkbGetIndicatorState(display, XkbUseCoreKbd, &capsLockedMods);

        if(capsLockedMods == 1 || capsLockedMods == 3){
            settings->set(KEY_CAPSLOCK_STATE,true);
            capsLockState = true;

        }
        else{
            settings->set(KEY_CAPSLOCK_STATE,false);
            capsLockState = false;

        }
        if (stInstalled && !ksettings->get(SHOW_TIP_KEY).toBool()){
            qWarning("MediaKey Tip is Closed\n");
            XCloseDisplay (display);
            return;
        }

        if (ifaceScreenSaver->isValid()){
            QDBusReply<bool>reply = ifaceScreenSaver->call("GetLockState");
            if (reply.isValid()){
                if (reply.value()){
                    qWarning("MediaKey Tip is Closed because ScreenLock\n");
                    XCloseDisplay (display);
                    return;
                }
            }
        }
        if(capsLockState)
        {
            m_statusWidget->setIcons("ukui-capslock-on");
            m_statusWidget->showWidget();

        }
        else
        {
            m_statusWidget->setIcons("ukui-capslock-off");
            m_statusWidget->showWidget();
        }

    }
    XCloseDisplay (display);
}

bool KeyboardManager::isCloudDesktopTop()
{
    QDBusInterface kwinInterface("org.ukui.KWin",
                                  "/KWin",
                                  "org.ukui.KWin",
                                  QDBusConnection::sessionBus());

    if(!kwinInterface.isValid()) {
        USD_LOG(LOG_DEBUG,"interface is unvalid");
        return false;
    }

    QDBusReply<bool> replay = kwinInterface.call("isDisableGlobalShortcuts");
    if(replay.isValid()) {
        USD_LOG(LOG_DEBUG,"isDisableGlobalShortcuts is valid :%d",replay.value());
        return replay.value();
    }
    USD_LOG(LOG_DEBUG,"isDisableGlobalShortcuts is unvalid");
    return false;
}

void KeyboardManager::numlock_install_xkb_callback ()
{
    if (!have_xkb)
        return;

//    connect(XEventMonitor::instance(), static_cast<void (XEventMonitor::*)(int)>(&XEventMonitor::keyRelease),
//            this, &KeyboardManager::XkbEventsFilter);
    connect(XEventMonitor::instance(), SIGNAL(keyRelease(int)),
            this, SLOT(XkbEventsFilter(int)));

}

void KeyboardManager::start_keyboard_idle_cb ()
{
    time->stop();
    have_xkb = 0;
    settings->set(KEY_NUMLOCK_REMEMBER,TRUE);
    XEventMonitor::instance()->start();

    /* Essential - xkb initialization should happen before */
     mKeyXkb->usd_keyboard_xkb_init (this);

#ifdef HAVE_X11_EXTENSIONS_XKB_H
    numlock_xkb_init (this);
#endif /* HAVE_X11_EXTENSIONS_XKB_H */

    /* apply current settings before we install the callback */
    usd_keyboard_manager_apply_settings (this);

//    QObject::connect(settings, &QGSettings::changed, this, &KeyboardManager::apply_settings);

    connect(settings,SIGNAL(changed(QString)),this,SLOT(apply_settings(QString)));

#ifdef HAVE_X11_EXTENSIONS_XKB_H
    numlock_install_xkb_callback();
#endif /* HAVE_X11_EXTENSIONS_XKB_H */

      apply_repeat (this);
      apply_numlock (this);
      apply_bell(this);
}
