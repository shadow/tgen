/*
 * See LICENSE for licensing information
 */

#include <stdarg.h>

#include <glib.h>

#include "tgen-log.h"

/* store a global pointer to the log filter */
GLogLevelFlags tgenLogFilterLevel = G_LOG_LEVEL_MESSAGE;

static const gchar* _tgenlog_logLevelToString(GLogLevelFlags logLevel) {
    switch (logLevel) {
        case G_LOG_LEVEL_ERROR:
            return "error";
        case G_LOG_LEVEL_CRITICAL:
            return "critical";
        case G_LOG_LEVEL_WARNING:
            return "warning";
        case G_LOG_LEVEL_MESSAGE:
            return "message";
        case G_LOG_LEVEL_INFO:
            return "info";
        case G_LOG_LEVEL_DEBUG:
            return "debug";
        default:
            return "default";
    }
}

void tgenlog_setLogFilterLevel(GLogLevelFlags level) {
    GLogLevelFlags oldLevel = tgenLogFilterLevel;
    if(oldLevel != level) {
        tgenLogFilterLevel = level;
        tgen_message("Changed log level filter from '%s' to '%s'",
                _tgenlog_logLevelToString(oldLevel), _tgenlog_logLevelToString(level));
    }
}

void tgenlog_printMessage(GLogLevelFlags level, const gchar* fileName, const gint lineNum,
        const gchar* functionName, const gchar* format, ...) {
    if(level > tgenLogFilterLevel) {
        return;
    }

    va_list vargs;
    va_start(vargs, format);

    gchar* fileStr = fileName ? g_path_get_basename(fileName) : g_strdup("n/a");
    const gchar* functionStr = functionName ? functionName : "n/a";

    GDateTime* dt = g_date_time_new_now_local();
    GString* newformat = g_string_new(NULL);

    g_string_append_printf(newformat, "%04i-%02i-%02i %02i:%02i:%02i %"G_GINT64_FORMAT".%06i [%s] [%s:%i] [%s] %s",
            g_date_time_get_year(dt), g_date_time_get_month(dt), g_date_time_get_day_of_month(dt),
            g_date_time_get_hour(dt), g_date_time_get_minute(dt), g_date_time_get_second(dt),
            g_date_time_to_unix(dt), g_date_time_get_microsecond(dt),
            _tgenlog_logLevelToString(level), fileStr, lineNum, functionName, format);

    gchar* message = g_strdup_vprintf(newformat->str, vargs);
    g_print("%s\n", message);
    g_free(message);

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);
    g_free(fileStr);

    va_end(vargs);
}
