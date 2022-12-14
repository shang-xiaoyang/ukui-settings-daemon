/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*-
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
#ifndef CLIB_SYSLOG_H
#define CLIB_SYSLOG_H
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "usd_global_define.h"
#define LOG_LEVEL LOG_DEBUG

#ifdef __cplusplus
extern "C" {
#endif


#include<time.h>

#define SYS_LOG(logLevel,...) {\
   syslog_info(logLevel, MODULE_NAME, __FILE__, __func__, __LINE__, ##__VA_ARGS__);\
}

#define USD_LOG(logLevel,...) {\
   syslog_to_self_dir(logLevel, MODULE_NAME, __FILE__, __func__, __LINE__, ##__VA_ARGS__);\
}

#define USD_LOG_SHOW_OUTPUT(output) USD_LOG(LOG_DEBUG,":%s (%s)(%s) use [%s] mode at (%dx%d) id %d %s primary id:%s,rotation:%d",  \
    output->name().toLatin1().data(), output->isConnected()? "connect":"disconnect", output->isEnabled()? "Enale":"Disable",    \
        output->currentModeId().toLatin1().data(), output->pos().x(), output->pos().y(),output->id(), output->isPrimary()? "is":"is't", output->hash().toLatin1().data(), output->rotation())


#define USD_LOG_SHOW_OUTPUT_NOID(output) USD_LOG(LOG_DEBUG,":%s (%s)(%s) use [%s] mode at (%dx%d) ",  \
    output->name().toLatin1().data(), output->isConnected()? "connect":"disconnect", output->isEnabled()? "Enale":"Disable",    \
        output->currentModeId().toLatin1().data(), output->pos().x(), output->pos().y())

#define USD_LOG_SHOW_PARAM1(a) USD_LOG(LOG_DEBUG,"%s : %d",#a,a)
#define USD_LOG_SHOW_PARAMF(a) USD_LOG(LOG_DEBUG,"%s : %f",#a,a)
#define USD_LOG_SHOW_PARAMS(a) USD_LOG(LOG_DEBUG,"%s : %s",#a,a)
#define USD_LOG_SHOW_PARAM2(a,b) USD_LOG(LOG_DEBUG,"%s : %d,%s : %d",#a,a,#b, b)
#define USD_LOG_SHOW_PARAM3(a,b,c) USD_LOG(LOG_DEBUG,"%s : %d,%s : %d",#a, a, #b, b, #c, c)
#define USD_LOG_SHOW_PARAM4(a,b,c,d) USD_LOG(LOG_DEBUG,"%s : %d,%s : %d",#a, a, #b, b, #c, c, #d, d)


#define SYS_LOG_SHOW_PARAM1(a) SYS_LOG(LOG_DEBUG,"%s : %d",#a,a)
#define SYS_LOG_SHOW_PARAMS(a) SYS_LOG(LOG_DEBUG,"%s : %s",#a,a)
#define SYS_LOG_SHOW_PARAM2(a,b) SYS_LOG(LOG_DEBUG,"%s : %d,%s : %d",#a,a,#b, b)
#define SYS_LOG_SHOW_PARAM3(a,b,c) SYS_LOG(LOG_DEBUG,"%s : %d,%s : %d",#a, a, #b, b, #c, c)
#define SYS_LOG_SHOW_PARAM4(a,b,c,d) SYS_LOG(LOG_DEBUG,"%s : %d,%s : %d",#a, a, #b, b, #c, c, #d, d)


#define USD_LOG_SHOW_PARAM2F(a,b) USD_LOG(LOG_DEBUG,"%s : %f,%s : %f",#a,a,#b, b)
#define CHECK_PROJECT(A) ifdef A
#define CHECK_OVER  endif

/*
* ?????????????????????
* @param category: ??????
* @param facility: ????????????
* @return
*      void
*/
void syslog_init(const char *category, int facility);

/*
* ???????????????system log?????????LOG_INFO??????
* @param loglevel: ????????????
* @param file: ?????????
* @param function: ??????
* @param line: ??????
* @param fmt: ??????????????????
* @return
*      void
*/
void syslog_info(int logLevel, const char *moduleName, const char *fileName, const char *functionName, int line, const char* fmt, ...);
void syslog_to_self_dir(int logLevel, const char *moduleName, const char *fileName, const char *functionName, int line, const char* fmt, ...);

/*
* ????????????????????????
*/
int CheckProcessAlive(const char *pName);

#ifdef __cplusplus
}
#endif //__cplusplus


#endif
