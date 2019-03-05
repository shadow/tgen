/*
 * See LICENSE for licensing information
 */

#ifndef SRC_TGEN_LOG_H_
#define SRC_TGEN_LOG_H_

#include <stdarg.h>

#include <glib.h>

void tgenlog_setLogFilterLevel(GLogLevelFlags level);

void tgenlog_printMessage(GLogLevelFlags level, const gchar* fileName, const gint lineNum,
        const gchar* functionName, const gchar* format, ...);

#define tgen_error(...)     {tgenlog_printMessage(G_LOG_LEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);}
#define tgen_critical(...)  {tgenlog_printMessage(G_LOG_LEVEL_CRITICAL, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);}
#define tgen_warning(...)   {tgenlog_printMessage(G_LOG_LEVEL_WARNING, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);}
#define tgen_message(...)   {tgenlog_printMessage(G_LOG_LEVEL_MESSAGE, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);}
#define tgen_info(...)      {tgenlog_printMessage(G_LOG_LEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);}
//#ifdef DEBUG
#if 1
#define tgen_debug(...)     {tgenlog_printMessage(G_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);}
#else
#define tgen_debug(...)
#endif

#endif /* SRC_TGEN_LOG_H_ */
