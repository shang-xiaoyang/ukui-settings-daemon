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
#include <QX11Info>
#include <QGuiApplication>
#include <QRect>
#include <QDebug>
#include "clib-syslog.h"
#include "xcbeventlistener.h"

XCBEventListener::XCBEventListener()
{
    xcb_connection_t *conn = QX11Info::connection();
    qDebug()<<QLatin1String(__FUNCTION__)<<":"<<__LINE__<<":";
    xcb_prefetch_extension_data(conn, &xcb_randr_id);
    qDebug()<<QLatin1String(__FUNCTION__)<<":"<<__LINE__<<":";
    auto cookie = xcb_randr_query_version(conn, XCB_RANDR_MAJOR_VERSION, XCB_RANDR_MINOR_VERSION);
    qDebug()<<QLatin1String(__FUNCTION__)<<":"<<__LINE__<<":";
    const auto *queryExtension = xcb_get_extension_data(conn, &xcb_randr_id);
    qDebug()<<QLatin1String(__FUNCTION__)<<":"<<__LINE__<<":";
    if (!queryExtension) {
        SYS_LOG(LOG_ERR,"Fail to query for xrandr extension")
        return;
    }
    if (!queryExtension->present) {
        SYS_LOG(LOG_ERR,"XRandR extension is not present at all")
        return;
    }

    xcb_generic_error_t *error = nullptr;
    auto *versionReply = xcb_randr_query_version_reply(conn, cookie, &error);
    Q_ASSERT_X(versionReply, "xrandrxcbhelper", "Query to fetch xrandr version failed");
    if (error) {
        SYS_LOG(LOG_WARNING,"Error while querying for xrandr version: %d", error->error_code)
    }
    m_versionMajor = versionReply->major_version;
    m_versionMinor = versionReply->minor_version;
    free(versionReply);


    uint32_t rWindow = QX11Info::appRootWindow();
    m_window = xcb_generate_id(conn);
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, m_window,
                      rWindow,
                      0, 0, 1, 1, 0, XCB_COPY_FROM_PARENT,
                      XCB_COPY_FROM_PARENT, 0, nullptr);

    xcb_randr_select_input(conn, m_window,
            XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE
   );

    qApp->installNativeEventFilter(this);
}

XCBEventListener::~XCBEventListener()
{
    if (m_window && QX11Info::connection()) {
        xcb_destroy_window(QX11Info::connection(), m_window);
    }
}


bool XCBEventListener::nativeEventFilter(const QByteArray &eventType, void *message, long *result)
{
    if (eventType != "xcb_generic_event_t") {
        return false;
    }

    auto *e = static_cast<xcb_generic_event_t *>(message);
    const uint8_t xEventType = e->response_type & ~0x80;

    if (xEventType == m_randrBase + XCB_RANDR_NOTIFY) {
         auto *randrEvent = reinterpret_cast<xcb_randr_notify_event_t*>(e);
         xcb_randr_output_change_t output = randrEvent->u.oc;
         if(randrEvent->subCode == XCB_RANDR_NOTIFY_OUTPUT_CHANGE) {
            qDebug()<<QLatin1String(__FUNCTION__)<<":"<<__LINE__<<":"<<output.connection;
            qDebug()<<QLatin1String(__FUNCTION__)<<":"<<__LINE__<<":"<<output.output;
            qDebug()<<QLatin1String(__FUNCTION__)<<":"<<__LINE__<<":"<<output.crtc;
         }
    }
    return false;
}
