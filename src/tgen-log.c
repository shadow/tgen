/*
 * See LICENSE for licensing information
 */

#include <stdarg.h>

#include <glib.h>

#include "tgen-log.h"

typedef struct _CachedLogMessage CachedLogMessage;
struct _CachedLogMessage {
    GLogLevelFlags level;
    gchar* str;
};

/* store a global pointer to the log filter */
GLogLevelFlags tgenLogFilterLevel = 0;

/* we cache messages until a log level is set */
GQueue* tgenLogMessageCache = NULL;

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

static void _tgenlog_flushMessageCache() {
    g_assert(tgenLogMessageCache);

    while(!g_queue_is_empty(tgenLogMessageCache)) {
        CachedLogMessage* message = g_queue_pop_head(tgenLogMessageCache);
        if(message) {
            if(message->str) {
                if(message->level <= tgenLogFilterLevel || tgenLogFilterLevel == 0) {
                    g_print("%s\n", message->str);
                }
                g_free(message->str);
            }
            g_free(message);
        }
    }

    g_queue_free(tgenLogMessageCache);
    tgenLogMessageCache = NULL;
}

static void _tgenlog_cacheMessage(gchar* messageStr, GLogLevelFlags level) {
    if(tgenLogMessageCache == NULL) {
        tgenLogMessageCache = g_queue_new();
    }
    CachedLogMessage* message = g_new0(CachedLogMessage, 1);
    message->str = messageStr;
    message->level = level;
    g_queue_push_tail(tgenLogMessageCache, message);
}

void tgenlog_setLogFilterLevel(GLogLevelFlags level) {
    GLogLevelFlags oldLevel = tgenLogFilterLevel;
    if(oldLevel != level) {
        tgenLogFilterLevel = level;
        tgen_message("Changed log level filter from '%s' to '%s'",
                _tgenlog_logLevelToString(oldLevel), _tgenlog_logLevelToString(level));
    }
    /* now that we set a level we can stop cacheing */
    if(tgenLogMessageCache) {
        _tgenlog_flushMessageCache();
    }
}

void tgenlog_printMessage(GLogLevelFlags level, const gchar* fileName, const gint lineNum,
        const gchar* functionName, const gchar* format, ...) {
    if(level > tgenLogFilterLevel && tgenLogFilterLevel > 0) {
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

    gchar* messageStr = g_strdup_vprintf(newformat->str, vargs);

    /* do we print right away, or cache it until we set a level */
    if(tgenLogFilterLevel > 0) {
        /* we already set a level */
        if(tgenLogMessageCache) {
            /* we have a level, so make sure we don't cache any more messages */
            _tgenlog_flushMessageCache();
        }
        g_print("%s\n", messageStr);
        g_free(messageStr);
    } else {
        _tgenlog_cacheMessage(messageStr, level);
    }

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);
    g_free(fileStr);

    va_end(vargs);
}
