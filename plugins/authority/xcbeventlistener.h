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
#ifndef XCBEVENTLISTENER_H
#define XCBEVENTLISTENER_H
#include <QObject>
#include <QLoggingCategory>
#include <QAbstractNativeEventFilter>
#include <QRect>

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/render.h>
class XCBEventListener: public QObject,
        public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    XCBEventListener();
    ~XCBEventListener() override;

    bool nativeEventFilter(const QByteArray& eventType, void* message, long int* result) override;

protected:
    bool m_isRandrPresent;
    bool m_event11;
    uint8_t m_randrBase;
    uint8_t m_randrErrorBase;
    uint8_t m_majorOpcode;
    uint32_t m_versionMajor;
    uint32_t m_versionMinor;

    uint32_t m_window;
};

#endif // XCBEVENTLISTENER_H
