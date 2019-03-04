/*
 * See LICENSE for licensing information
 */

#ifndef SRC_TGEN_OPTIONPARSER_H_
#define SRC_TGEN_OPTIONPARSER_H_

typedef struct _TGenOptionUInt16 {
    gboolean isSet;
    guint16 value;
} TGenOptionUInt16;

typedef struct _TGenOptionUInt32 {
    gboolean isSet;
    guint32 value;
} TGenOptionUInt32;

typedef struct _TGenOptionUInt64 {
    gboolean isSet;
    guint64 value;
} TGenOptionUInt64;

typedef struct _TGenOptionBoolean {
    gboolean isSet;
    gboolean value;
} TGenOptionBoolean;

typedef struct _TGenOptionString {
    gboolean isSet;
    gchar* value;
} TGenOptionString;

typedef struct _TGenOptionLogLevel {
    gboolean isSet;
    GLogLevelFlags value;
} TGenOptionLogLevel;

typedef struct _TGenOptionPeer {
    gboolean isSet;
    TGenPeer* value;
} TGenOptionPeer;

typedef struct _TGenOptionPool {
    gboolean isSet;
    TGenPool* value;
} TGenOptionPool;

GError* tgenoptionparser_parseBoolean(const gchar* attributeName,
        const gchar* booleanStr, TGenOptionBoolean* out);

GError* tgenoptionparser_parseUInt16(const gchar* attributeName,
        const gchar* intStr, TGenOptionUInt16* out);

GError* tgenoptionparser_parseUInt32(const gchar* attributeName,
        const gchar* intStr, TGenOptionUInt32* out);

GError* tgenoptionparser_parseUInt64(const gchar* attributeName,
        const gchar* intStr, TGenOptionUInt64* out);

GError* tgenoptionparser_parseString(const gchar* attributeName,
        const gchar* stringStr, TGenOptionString* out);

GError* tgenoptionparser_parseBytes(const gchar* attributeName,
        const gchar* byteStr, TGenOptionUInt64* out);

GError* tgenoptionparser_parseTime(const gchar* attributeName,
        const gchar* timeStr, TGenOptionUInt64* out);

GError* tgenoptionparser_parseTimeList(const gchar* attributeName,
        const gchar* timeStr, TGenOptionPool* out);

GError* tgenoptionparser_parsePeer(const gchar* attributeName,
        const gchar* peerStr, TGenOptionPeer* out);

GError* tgenoptionparser_parsePeerList(const gchar* attributeName,
        const gchar* peersStr, TGenOptionPool* out);

GError* tgenoptionparser_parseLogLevel(const gchar* attributeName,
        const gchar* loglevelStr, TGenOptionLogLevel* out);

#endif /* SRC_TGEN_OPTIONPARSER_H_ */
