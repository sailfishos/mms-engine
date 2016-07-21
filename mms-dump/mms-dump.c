/*
 * Copyright (C) 2013-2016 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <glib.h>
#include <wsputil.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char pname[] = "mms-dump";

enum app_ret_value {
    RET_OK,
    RET_ERR_CMDLINE,
    RET_ERR_IO,
    RET_ERR_DECODE
};

#define N_(a) G_N_ELEMENTS(a)

#define MMS_DUMP_FLAG_VERBOSE (0x01)
#define MMS_DUMP_FLAG_DATA    (0x02)
#define MMS_DUMP_FLAG_EXTRACT (0x04)

#define HEADER_CONTENT_DISPOSITION  "Content-Disposition"
#define PARAM_FILENAME              "Filename"
#define PARAM_NAME                  "Name"

#define WSP_QUOTE (127)

struct mms_named_value {
    const char* name;
    unsigned int value;
};

struct mms_header_param {
    char* name;
    char* value;
};

struct mms_header {
    char* name;
    char* value;
    char* dump;
    struct mms_header_param** params;
    unsigned int num_params;
};

struct mms_headers {
    struct mms_header** headers;
    unsigned int num_headers;
};

typedef gboolean
(*mms_value_decoder)(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags);

#define MMS_CONTENT_TYPE  "application/vnd.wap.mms-message"

#define MMS_WELL_KNOWN_HEADERS(h) \
    h(BCC,                    "Bcc",                          0x01, etext    )\
    h(CC,                     "Cc",                           0x02, etext    )\
    h(CONTENT_LOCATION,       "X-Mms-Content-Location",       0x03, text     )\
    h(CONTENT_TYPE,           "Content-Type",                 0x04, unknown  )\
    h(DATE,                   "Date",                         0x05, date     )\
    h(DELIVERY_REPORT,        "X-Mms-Delivery-Report",        0x06, bool     )\
    h(DELIVERY_TIME,          "X-Mms-Delivery-Time",          0x07, unknown  )\
    h(EXPIRY,                 "X-Mms-Expiry",                 0x08, expiry   )\
    h(FROM,                   "From",                         0x09, from     )\
    h(MESSAGE_CLASS,          "X-Mms-Message-Class",          0x0A, mclass   )\
    h(MESSAGE_ID,             "Message-ID",                   0x0B, text     )\
    h(MESSAGE_TYPE,           "X-Mms-Message-Type",           0x0C, mtype    )\
    h(MMS_VERSION,            "X-Mms-MMS-Version",            0x0D, version  )\
    h(MESSAGE_SIZE,           "X-Mms-Message-Size",           0x0E, long     )\
    h(PRIORITY,               "X-Mms-Priority",               0x0F, prio     )\
    h(READ_REPORT,            "X-Mms-Read-Report",            0x10, bool     )\
    h(REPORT_ALLOWED,         "X-Mms-Report-Allowed",         0x11, bool     )\
    h(RESPONSE_STATUS,        "X-Mms-Response-Status",        0x12, respstat )\
    h(RESPONSE_TEXT,          "X-Mms-Response-Text",          0x13, etext    )\
    h(SENDER_VISIBILITY,      "X-Mms-Sender-Visibility",      0x14, visiblty )\
    h(STATUS,                 "X-Mms-Status",                 0x15, status   )\
    h(SUBJECT,                "Subject",                      0x16, etext    )\
    h(TO,                     "To",                           0x17, etext    )\
    h(TRANSACTION_ID,         "X-Mms-Transaction-Id",         0x18, text     )\
    h(RETRIEVE_STATUS,        "X-Mms-Retrieve-Status",        0x19, retrieve )\
    h(RETRIEVE_TEXT,          "X-Mms-Retrieve-Text",          0x1A, etext    )\
    h(READ_STATUS,            "X-Mms-Read-Status",            0x1B, rstatus  )\
    h(REPLY_CHARGING,         "X-Mms-Reply-Charging",         0x1C, short    )\
    h(REPLY_CHARGING_DEADLINE,"X-Mms-Reply-Charging-Deadline",0x1D, unknown  )\
    h(REPLY_CHARGING_ID,      "X-Mms-Reply-Charging-ID",      0x1E, text     )\
    h(REPLY_CHARGING_SIZE,    "X-Mms-Reply-Charging-Size",    0x1F, long     )\
    h(PREVIOUSLY_SENT_BY,     "X-Mms-Previously-Sent-By",     0x20, prevby   )\
    h(PREVIOUSLY_SENT_DATE,   "X-Mms-Previously-Sent-Date",   0x21, prevdate )

#define WSP_WELL_KNOWN_HEADERS(h) \
    h(CONTENT_LOCATION,       "Content-Location",             0x0E, text     )\
    h(CONTENT_DISPOSITION,    HEADER_CONTENT_DISPOSITION,     0x2E, contdisp )\
    h(CONTENT_ID,             "Content-ID",                   0x40, quote    )\
    h(CONTENT_DISPOSITION2,   HEADER_CONTENT_DISPOSITION,     0x45, contdisp )

#define MMS_HEADER_ID(id) MMS_HEADER_##id
#define MMS_PART_HEADER_ID(id) MMS_PART_HEADER_##id

typedef enum mms_header_id {
    MMS_HEADER_INVALID,
#define h(id,n,x,t) MMS_HEADER_ID(id) = x,
    MMS_WELL_KNOWN_HEADERS(h)
#undef h
    MMS_HEADER_END,
} MMS_HEADER;

typedef enum mms_part_header_id {
#define h(id,n,x,t) MMS_PART_HEADER_ID(id) = x,
    WSP_WELL_KNOWN_HEADERS(h)
#undef h
} MMS_PART_HEADER;

static
const char*
mms_message_well_known_header_name(
    int hdr)
{
    static char unknown[6];
    switch (hdr) {
#define h(id,n,x,t) case MMS_HEADER_ID(id): return n;
    MMS_WELL_KNOWN_HEADERS(h)
#undef h
    default:
        snprintf(unknown, sizeof(unknown), "0x%02X", hdr);
        unknown[sizeof(unknown)-1] = 0;
        return unknown;
    }
}

static
const char*
mms_part_well_known_header_name(
    int hdr)
{
    static char unknown[6];
    switch (hdr) {
#define h(id,n,x,t) case MMS_PART_HEADER_ID(id): return n;
    WSP_WELL_KNOWN_HEADERS(h)
#undef h
    default:
        snprintf(unknown, sizeof(unknown), "0x%02X", hdr);
        unknown[sizeof(unknown)-1] = 0;
        return unknown;
    }
}

static
const struct mms_named_value*
mms_find_named_value(
    const struct mms_named_value* values,
    unsigned int num_values,
    unsigned int value)
{
    unsigned int i;
    for (i=0; i<num_values; i++) {
        if (value == values[i].value) {
            return values + i;
        }
    }
    return NULL;
}

static
struct mms_header*
mms_header_new(
    const char* name)
{
    struct mms_header* header = g_slice_new0(struct mms_header);
    header->name = g_strdup(name);
    return header;
}

static
struct mms_header*
mms_header_new_content_type()
{
    return mms_header_new("Content-Type");
}

static
void
mms_header_free(
    struct mms_header* header)
{
    if (header) {
        unsigned int i;
        for (i=0; i<header->num_params; i++) {
            struct mms_header_param* param = header->params[i];
            g_free(param->name);
            g_free(param->value);
            g_slice_free(struct mms_header_param, param);
        }
        g_free(header->name);
        g_free(header->value);
        g_free(header->dump);
        g_free(header->params);
        g_slice_free(struct mms_header, header);
    }
}

static
void
mms_headers_free(
    struct mms_headers* headers)
{
    if (headers) {
        unsigned int i;
        for (i=0; i<headers->num_headers; i++) {
            mms_header_free(headers->headers[i]);
        }
        g_free(headers->headers);
        g_slice_free(struct mms_headers, headers);
    }
}

static
void
mms_header_print(
    const struct mms_header* header,
    const char* prefix,
    unsigned int flags)
{
    if (header) {
        unsigned int i;
        printf("%s%s: ", prefix, header->name);
        if (header->value) printf("%s", header->value);
        for (i=0; i<header->num_params; i++) {
            struct mms_header_param* param = header->params[i];
            printf("; %s=%s", param->name, param->value);
        }
        if ((flags & MMS_DUMP_FLAG_VERBOSE) && header->dump) {
            printf(" (%s)", header->dump);
        }
        printf("\n");
    }
}

static
void
mms_headers_print(
    const struct mms_headers* headers,
    const char* prefix,
    unsigned int flags)
{
    if (headers) {
        unsigned int i;
        for (i=0; i<headers->num_headers; i++) {
            mms_header_print(headers->headers[i], prefix, flags);
        }
    }
}

static
const char*
mms_header_find(
    const struct mms_header* header,
    const char* name)
{
    if (header && name) {
        unsigned int i;
        for (i=0; i<header->num_params; i++) {
            const struct mms_header_param* param = header->params[i];
            if (!g_ascii_strcasecmp(param->name, name)) {
                return param->value;
            }
        }
    }
    return NULL;
}

static
const struct mms_header*
mms_headers_find(
    const struct mms_headers* headers,
    const char* name)
{
    if (headers && name) {
        unsigned int i;
        for (i=0; i<headers->num_headers; i++) {
            const struct mms_header* header = headers->headers[i];
            if (!g_ascii_strcasecmp(header->name, name)) {
                return header;
            }
        }
    }
    return NULL;
}

static
const char*
mms_headers_find_filename(
    const struct mms_header* content_type,
    const struct mms_headers* headers)
{
    const struct mms_header* disp = mms_headers_find(headers,
        HEADER_CONTENT_DISPOSITION);
    if (disp) {
        const char* filename = mms_header_find(disp, PARAM_FILENAME);
        if (filename) {
            return filename;
        }
    }
    return mms_header_find(content_type, PARAM_NAME);
}

static
void
mms_header_set_count_param(
    struct mms_header* header,
    unsigned int count)
{
    header->num_params = 1;
    header->params = g_new(struct mms_header_param*, header->num_params);
    header->params[0] = g_slice_new0(struct mms_header_param);
    header->params[0]->name = g_strdup("Count");
    header->params[0]->value = g_strdup_printf("%u", count);
}

static
char*
mms_hexdump(
    const guint8* val,
    unsigned int len)
{
    if (len > 0) {
        unsigned int i;
        GString* buf = g_string_new_len(NULL, len*3 - 1);
        g_string_append_printf(buf, "%02X", val[0]);
        for (i=1; i<len; i++) {
            g_string_append_printf(buf, " %02X", val[i]);
        }
        return g_string_free(buf, FALSE);
    }
    return g_strdup("");
}

static
void
mms_header_dump(
    struct mms_header* header,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    if ((flags & MMS_DUMP_FLAG_VERBOSE) && len > 0) {
        header->dump = mms_hexdump(val, len);
    }
}

static
gboolean
mms_value_decode_unknown(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    if (type == WSP_VALUE_TYPE_TEXT) {
        if (val[0] == WSP_QUOTE) val++;
        header->value = g_strdup((char*)val);
        mms_header_dump(header, val, len, flags);
    } else if (len == 1) {
        header->value = g_strdup_printf("0x%02X", val[0]);
        if (flags & MMS_DUMP_FLAG_VERBOSE) {
            header->dump = g_strdup_printf("%u", val[0]);
        }
    } else if (len > 1) {
        header->value = mms_hexdump(val, len);
    }
    return TRUE;
}

static
gboolean
mms_value_decode_quote(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    /* Quoted-string = <Octet 34> *TEXT End-of-string */
    if (type == WSP_VALUE_TYPE_TEXT && len > 0 && val[0] == 0x22) {
        header->value = g_strdup((char*)val+1);
        mms_header_dump(header, val, len, flags);
        return TRUE;
    }
    return FALSE;
}

static
gboolean
mms_value_decode_enum(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    const struct mms_named_value* values,
    unsigned int num_values,
    unsigned int flags)
{
    if (type == WSP_VALUE_TYPE_SHORT && len == 1) {
        const struct mms_named_value* nv;
        nv = mms_find_named_value(values, num_values, val[0]);
        if (nv) {
            header->value = g_strdup(nv->name);
            if (flags & MMS_DUMP_FLAG_VERBOSE) {
                header->dump = g_strdup_printf("%u", nv->value);
            }
            return TRUE;
        }
    }
    return mms_value_decode_unknown(header, type, val, len, flags);
}

static
gboolean
mms_value_decode_version(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    if (type == WSP_VALUE_TYPE_SHORT && len == 1) {
        const unsigned int value = val[0];
        header->value = g_strdup_printf("%u.%u",
            (val[0] & 0x70) >> 4, val[0] & 0x0f);
        if (flags & MMS_DUMP_FLAG_VERBOSE) {
            header->dump = g_strdup_printf("%u", value);
        }
        return TRUE;
    }
    return mms_value_decode_unknown(header, type, val, len, flags);
}

static
gboolean
mms_value_decode_long(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    unsigned long long_val;
    if (type == WSP_VALUE_TYPE_LONG && len <= sizeof(long_val)) {
        unsigned int i;
        for (long_val = 0, i=0; i<len; i++) {
            long_val = ((long_val << 8) | val[i]);
        }
        header->value = g_strdup_printf("%lu", long_val);
        return TRUE;
    }
    return mms_value_decode_unknown(header, type, val, len, flags);
}

static
char*
mms_value_format_date(
    time_t t)
{
    char date[64];
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S%z", localtime(&t));
    return g_strdup(date);
}

static
gboolean
mms_value_decode_date_value(
    const guint8* val,
    unsigned int len,
    time_t* t)
{
    if (len <= sizeof(t)) {
        unsigned int i;
        for (*t=0, i=0; i<len; i++) {
            *t = (((*t) << 8) | val[i]);
        }
        return TRUE;
    }
    return FALSE;
}

static
gboolean
mms_value_decode_date(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    time_t t;
    if (type == WSP_VALUE_TYPE_LONG &&
        mms_value_decode_date_value(val, len, &t)) {
        header->value = mms_value_format_date(t);
        if (flags & MMS_DUMP_FLAG_VERBOSE) {
            header->dump = g_strdup_printf("%lu", (unsigned long)t);
        }
        return TRUE;
    }
    return mms_value_decode_unknown(header, type, val, len, flags);
}

static
gboolean
mms_value_decode_bool(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    static const struct mms_named_value nv [] = {
        { "Yes", 128 },
        { "No",  129 }
    };
    return mms_value_decode_enum(header, type, val, len, nv, N_(nv), flags);
}

/* Message-type-value */
static
gboolean
mms_value_decode_mtype(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    static const struct mms_named_value nv [] = {
        { "M-Send.req",         128 },
        { "M-Send.conf",        129 },
        { "M-Notification.ind", 130 },
        { "M-Notifyresp.ind",   131 },
        { "M-Retrieve.conf",    132 },
        { "M-Acknowledge.ind",  133 },
        { "M-Delivery.ind",     134 },
        { "M-Read-Rec.ind",     135 },
        { "M-Read-Orig.ind",    136 },
        { "M-Forward.req",      137 },
        { "M-Forward.conf",     138 }
    };
    return mms_value_decode_enum(header, type, val, len, nv, N_(nv), flags);
}

/* Message-class-value */
static
gboolean
mms_value_decode_mclass(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    static const struct mms_named_value nv [] = {
        { "Personal",      128 },
        { "Advertisement", 129 },
        { "Informational", 130 },
        { "Auto",          131 }
    };
    return mms_value_decode_enum(header, type, val, len, nv, N_(nv), flags);
}

/* Priority-value */
static
gboolean
mms_value_decode_prio(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    static const struct mms_named_value nv [] = {
        { "Low",    128 },
        { "Normal", 129 },
        { "High",   130 }
    };
    return mms_value_decode_enum(header, type, val, len, nv, N_(nv), flags);
}

/* Retrieve-status-value */
static
gboolean
mms_value_decode_retrieve(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    static const struct mms_named_value nv [] = {
        { "Ok",                                  128 },
        { "Error-transient-failure",             192 },
        { "Error-transient-message-not-found",   193 },
        { "Error-transient-network-problem",     194 },
        { "Error-permanent-failure",             224 },
        { "Error-permanent-service-denied",      225 },
        { "Error-permanent-message-not-found",   226 },
        { "Error-permanent-content-unsupported", 227 }
    };
    return mms_value_decode_enum(header, type, val, len, nv, N_(nv), flags);
}

/* Read-status-value */
static
gboolean
mms_value_decode_rstatus(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    static const struct mms_named_value nv [] = {
        { "Read",    128 },
        { "Deleted", 129 }
    };
    return mms_value_decode_enum(header, type, val, len, nv, N_(nv), flags);
}

/* Status-value */
static
gboolean
mms_value_decode_status(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    static const struct mms_named_value nv [] = {
        { "Expired",       128 },
        { "Retrieved",     129 },
        { "Rejected",      130 },
        { "Deferred",      131 },
        { "Unrecognised",  132 },
        { "Indeterminate", 133 },
        { "Forwarded",     134 }
    };
    return mms_value_decode_enum(header, type, val, len, nv, N_(nv), flags);
}

/* Response-status-value */
static
gboolean
mms_value_decode_respstat(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    static const struct mms_named_value nv [] = {
        { "Ok",                                                  128 },
        { "Error-unspecified",                                   129 },
        { "Error-service-denied",                                130 },
        { "Error-message-format-corrupt",                        131 },
        { "Error-sending-address-unresolved",                    132 },
        { "Error-message-not-found",                             133 },
        { "Error-network-problem",                               134 },
        { "Error-content-not-accepted",                          135 },
        { "Error-unsupported-message",                           136 },
        { "Error-transient-failure",                             192 },
        { "Error-transient-sending-address-unresolved",          193 },
        { "Error-transient-message-not-found",                   194 },
        { "Error-transient-network-problem",                     195 },
        { "Error-transient-partial-success",                     196 },
        { "Error-permanent-failure",                             224 },
        { "Error-permanent-service-denied",                      225 },
        { "Error-permanent-message-format-corrupt",              226 },
        { "Error-permanent-sending-address-unresolved",          227 },
        { "Error-permanent-message-not-found",                   228 },
        { "Error-permanent-content-not-accepted",                229 },
        { "Error-permanent-reply-charging-limitations-not-met",  230 },
        { "Error-permanent-reply-charging-request-not-accepted", 231 },
        { "Error-permanent-reply-charging-forwarding-denied",    232 },
        { "Error-permanent-reply-charging-not-supported",        233 },
        { "Error-permanent-address-hiding-not-supported",        234 },
        { "Error-permanent-lack-of-prepaid",                     235 }
    };
    return mms_value_decode_enum(header, type, val, len, nv, N_(nv), flags);
}

/* Encoded-string-value */
static
gboolean
mms_value_decode_encoded_text(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len)
{
    /* http://www.iana.org/assignments/character-sets */
    static const struct mms_named_value nv [] = {
        { "US-ASCII",        3},
        { "ISO_8859-1",      4},
        { "ISO_8859-2",      5},
        { "ISO_8859-3",      6},
        { "ISO_8859-4",      7},
        { "ISO_8859-5",      8},
        { "ISO_8859-6",      9},
        { "ISO_8859-7",      10},
        { "ISO_8859-8",      11},
        { "ISO_8859-9",      12},
        { "ISO-8859-10",     13},
        { "Shift_JIS",       17},
        { "EUC-JP",          18},
        { "CP949",           36},
        { "ISO-2022-KR",     37},
        { "EUC-KR",          38},
        { "ISO-2022-JP",     39},
        { "ISO-2022-JP-2",   40},
        { "ISO_8859-6-E",    81},
        { "ISO_8859-6-I",    82},
        { "ISO_8859-8-E",    84},
        { "ISO_8859-8-I",    85},
        { "UTF-8",           106},
        { "ISO-8859-13",     109},
        { "ISO-8859-14",     110},
        { "ISO-8859-15",     111},
        { "ISO-8859-16",     112},
        { "GBK",             113},
        { "GB18030",         114},
        { "ISO-10646-UCS-2", 1000},
        { "ISO-10646-UCS-4", 1001},
        { "ISO-10646-J-1",   1004},
        { "UTF-7",           1012},
        { "UTF-16BE",        1013},
        { "UTF-16LE",        1014},
        { "UTF-16",          1015},
        { "UTF-32",          1017},
        { "UTF-32BE",        1018},
        { "UTF-32LE",        1019},
        { "GB2312",          2025},
        { "Big5",            2026},
        { "macintosh",       2027},
        { "KOI8-R",          2084},
        { "windows-874",     2109},
        { "windows-1250",    2250},
        { "windows-1251",    2251},
        { "windows-1252",    2252},
        { "windows-1253",    2253},
        { "windows-1254",    2254},
        { "windows-1255",    2255},
        { "windows-1256",    2256},
        { "windows-1257",    2257},
        { "windows-1258",    2258}
    };

    if (type == WSP_VALUE_TYPE_TEXT) {
        header->value = g_strdup((char*)val);
        return TRUE;
    } else if (type == WSP_VALUE_TYPE_LONG) {
        unsigned int charset = 0;
        unsigned int consumed = 0;
        if (wsp_decode_integer(val, len, &charset, &consumed)) {
            const struct mms_named_value* cs;
            cs = mms_find_named_value(nv, N_(nv), charset);
            if (cs) {
                char* tmp = NULL;
                const char* text = NULL;
                const char* dest_charset = "UTF-8";
                if (strcmp(dest_charset, cs->name)) {
                    gsize bytes_in, bytes_out;
                    text = tmp = g_convert((char*)val+consumed, len-consumed,
                        dest_charset, cs->name, &bytes_in, &bytes_out, NULL);
                } else {
                    if (val[consumed] == WSP_QUOTE) consumed++;
                    if (consumed <= len) text = (char*)val+consumed;
                }
                if (text) {
                    header->value = g_strdup(text);
                    g_free(tmp);
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

static
gboolean
mms_value_decode_etext(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    if (mms_value_decode_encoded_text(header, type, val, len)) {
        mms_header_dump(header, val, len, flags);
        return TRUE;
    } else {
        return mms_value_decode_unknown(header, type, val, len, flags);
    }
}

/* Sender-visibility-value */
static
gboolean
mms_value_decode_visiblty(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    static const struct mms_named_value nv [] = {
        { "Hide", 128 },
        { "Show", 129 },
    };
    return mms_value_decode_enum(header, type, val, len, nv, N_(nv), flags);
}

/* From-value */
static
gboolean
mms_value_decode_from(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    /*
     * Address-present-token Encoded-string-value | Insert-address-token
     * Address-present-token = <Octet 128>
     * Insert-address-token = <Octet 129>
     */
    if (type == WSP_VALUE_TYPE_LONG && len > 0) {
        if (val[0] == 0x81) {
            header->value = g_strdup("<Insert-address>");
            if (flags & MMS_DUMP_FLAG_VERBOSE) {
                header->dump = g_strdup_printf("%u", val[0]);
            }
            return TRUE;
        } else if (val[0] == 0x80 && len > 1) {
            enum wsp_value_type ftype;
            const void* fval = NULL;
            unsigned int flen = 0;
            if (wsp_decode_field(val+1, len-1, &ftype, &fval, &flen, NULL) &&
                mms_value_decode_etext(header, ftype, fval, flen, 0)) {
                mms_header_dump(header, val, len, flags);
                return TRUE;
            }
        }
    }
    return mms_value_decode_unknown(header, type, val, len, flags);
}

/* Expiry-value */
static
gboolean
mms_value_decode_expiry(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    /*
     * Absolute-token Date-value | Relative-token Delta-seconds-value
     * Absolute-token = <Octet 128>
     * Relative-token = <Octet 129>
     */
    gboolean ok = FALSE;
    enum wsp_value_type ftype;
    const void* fval = NULL;
    unsigned int flen = 0;
    if (type == WSP_VALUE_TYPE_LONG && len > 1 &&
        wsp_decode_field(val+1, len-1, &ftype, &fval, &flen, NULL)) {
        if (val[0] == 0x80 /* Absolute-token */) {
            ok = mms_value_decode_date(header, ftype, fval, flen, 0);
        } else if (val[0] == 0x81 /* Relative-token */) {
            time_t t;
            if (ftype == WSP_VALUE_TYPE_LONG && flen > 0 && flen <= sizeof(t)) {
                unsigned int i;
                for (t=0, i=0; i<flen; i++) {
                    t = ((t << 8) | ((guint8*)fval)[i]);
                }
                header->value = g_strdup_printf("+%u sec", (unsigned int)t);
                ok = TRUE;
            }
        }
    }
    if (ok) {
        mms_header_dump(header, val, len, flags);
        return TRUE;
    }
    return mms_value_decode_unknown(header, type, val, len, flags);
}

/* Previously-sent-by-value */
static
gboolean
mms_value_decode_prevby(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    /*
     * Value-length Forwarded-count-value Encoded-string-value
     * Forwarded-count-value = Integer-value
     */
    if (type == WSP_VALUE_TYPE_LONG) {
        unsigned int count = 0;
        unsigned int consumed = 0;
        if (wsp_decode_integer(val, len, &count, &consumed)) {
            const guint8* ptr = val + consumed;
            const unsigned int bytes_left = len - consumed;
            enum wsp_value_type ftype;
            const void* fval;
            unsigned int flen;
            if (wsp_decode_field(ptr, bytes_left, &ftype, &fval, &flen,
                &consumed) && consumed == bytes_left &&
                mms_value_decode_encoded_text(header, ftype, fval, flen)) {
                mms_header_set_count_param(header, count);
                mms_header_dump(header, val, len, flags);
                return TRUE;
            }
        }
    }
    return mms_value_decode_unknown(header, type, val, len, flags);
}

/* Previously-sent-date-value */
static
gboolean
mms_value_decode_prevdate(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    /*
     * Value-length Forwarded-count-value Date-value
     * Forwarded-count-value = Integer-value
     */
    if (type == WSP_VALUE_TYPE_LONG) {
        unsigned int count = 0;
        unsigned int consumed = 0;
        if (wsp_decode_integer(val, len, &count, &consumed)) {
            const guint8* ptr = val + consumed;
            const unsigned int bytes_left = len - consumed;
            enum wsp_value_type date_type;
            const void* date_val;
            unsigned int date_len;
            time_t t;
            if (wsp_decode_field(ptr, bytes_left, &date_type, &date_val,
                &date_len, &consumed) && consumed == bytes_left &&
                date_type == WSP_VALUE_TYPE_LONG &&
                mms_value_decode_date_value(date_val, date_len, &t)) {
                header->value = mms_value_format_date(t);
                mms_header_set_count_param(header, count);
                mms_header_dump(header, val, len, flags);
                return TRUE;
            }
        }
    }
    return mms_value_decode_unknown(header, type, val, len, flags);
}

static
void
mms_value_decode_wsp_params(
    struct mms_header* header,
    const unsigned char* pdu,
    unsigned int len)
{
    static const struct mms_named_value nv_p [] = {
        { "Q",                 WSP_PARAMETER_TYPE_Q                  },
        { "Charset",           WSP_PARAMETER_TYPE_CHARSET            },
        { "Level",             WSP_PARAMETER_TYPE_LEVEL              },
        { "Type",              WSP_PARAMETER_TYPE_TYPE               },
        { PARAM_NAME,          WSP_PARAMETER_TYPE_NAME_DEFUNCT       },
        { PARAM_FILENAME,      WSP_PARAMETER_TYPE_FILENAME_DEFUNCT   },
        { "Differences",       WSP_PARAMETER_TYPE_DIFFERENCES        },
        { "Padding",           WSP_PARAMETER_TYPE_PADDING            },
        { "Type",              WSP_PARAMETER_TYPE_CONTENT_TYPE       },
        { "Start",             WSP_PARAMETER_TYPE_START_DEFUNCT      },
        { "Start-info",        WSP_PARAMETER_TYPE_START_INFO_DEFUNCT },
        { "Comment",           WSP_PARAMETER_TYPE_COMMENT_DEFUNCT    },
        { "Domain",            WSP_PARAMETER_TYPE_DOMAIN_DEFUNCT     },
        { "Max-Age",           WSP_PARAMETER_TYPE_MAX_AGE            },
        { "Path",              WSP_PARAMETER_TYPE_PATH_DEFUNCT       },
        { "Secure",            WSP_PARAMETER_TYPE_SECURE             },
        { "SEC",               WSP_PARAMETER_TYPE_SEC                },
        { "MAC",               WSP_PARAMETER_TYPE_MAC                },
        { "Creation-date",     WSP_PARAMETER_TYPE_CREATION_DATE      },
        { "Modification-date", WSP_PARAMETER_TYPE_MODIFICATION_DATE  },
        { "Read-date",         WSP_PARAMETER_TYPE_READ_DATE          },
        { "Size",              WSP_PARAMETER_TYPE_SIZE               },
        { PARAM_NAME,          WSP_PARAMETER_TYPE_NAME               },
        { PARAM_FILENAME,      WSP_PARAMETER_TYPE_FILENAME           },
        { "Start",             WSP_PARAMETER_TYPE_START              },
        { "Start-info",        WSP_PARAMETER_TYPE_START_INFO         },
        { "Comment",           WSP_PARAMETER_TYPE_COMMENT            },
        { "Domain",            WSP_PARAMETER_TYPE_DOMAIN             },
        { "Path",              WSP_PARAMETER_TYPE_PATH               }
    };

    GPtrArray* tmp = g_ptr_array_new();
    struct wsp_parameter_iter pi;
    struct wsp_parameter p;

    wsp_parameter_iter_init(&pi, pdu, len);
    while (wsp_parameter_iter_next(&pi, &p)) {
        struct mms_header_param* hp = g_slice_new0(struct mms_header_param);
        const struct mms_named_value* nv =
            mms_find_named_value(nv_p, N_(nv_p), p.type);
        hp->name = nv ? g_strdup(nv->name) : g_strdup_printf("0x%02x", p.type);
        switch (p.value) {
        case WSP_PARAMETER_VALUE_TEXT:
            hp->value = g_strdup(p.text[0] == '"' ? (p.text + 1) : p.text);
            break;
        case WSP_PARAMETER_VALUE_INT:
            hp->value = g_strdup_printf("%u", p.integer);
            break;
        case WSP_PARAMETER_VALUE_DATE:
            hp->value = mms_value_format_date(p.date);
            break;
        case WSP_PARAMETER_VALUE_Q:
            hp->value = g_strdup_printf("%g", p.q);
            break;
        }
        g_ptr_array_add(tmp, hp);
    }

    header->num_params = tmp->len;
    header->params = (struct mms_header_param**)g_ptr_array_free(tmp, FALSE);
}

static
gboolean
mms_value_decode_contdisp(
    struct mms_header* header,
    enum wsp_value_type type,
    const guint8* val,
    unsigned int len,
    unsigned int flags)
{
    /*
     * Content-disposition-value = Value-length Disposition *(Parameter)
     *
     * Value-length = Short-length | (Length-quote Length)
     * Short-length = <Any octet 0-30>
     * Length-quote = <Octet 31>
     * Length = Uintvar-integer
     *
     * Disposition = Form-data | Attachment | Inline | Token-text
     * Form-data = <Octet 128>
     * Attachment = <Octet 129>
     * Inline = <Octet 130>
     */
    static const struct mms_named_value nv_d [] = {
        { "Form-data",  128 },
        { "Attachment", 129 },
        { "Inline",     130 }
    };

    if ((type == WSP_VALUE_TYPE_LONG ||
         type == WSP_VALUE_TYPE_SHORT) && len > 0) {
        const struct mms_named_value* nv;
        nv = mms_find_named_value(nv_d, N_(nv_d), val[0]);
        if (nv) {
            header->value = g_strdup(nv->name);
            mms_value_decode_wsp_params(header, val + 1, len - 1);
            mms_header_dump(header, val, len, flags);
            return TRUE;
        }
    }
    return mms_value_decode_unknown(header, type, val, len, flags);
}

#define mms_value_decode_short  mms_value_decode_unknown
#define mms_value_decode_text   mms_value_decode_unknown

static
mms_value_decoder
mms_message_value_decoder_for_header(
    const char* hdr)
{
#define h(id,n,x,t) if (!strcmp(n,hdr)) return &mms_value_decode_##t;
    MMS_WELL_KNOWN_HEADERS(h);
#undef h
    return &mms_value_decode_unknown;
}

static
mms_value_decoder
mms_part_value_decoder_for_header(
    const char* hdr)
{
#define h(id,n,x,t) if (!strcmp(n,hdr)) return &mms_value_decode_##t;
    WSP_WELL_KNOWN_HEADERS(h);
#undef h
    return &mms_value_decode_unknown;
}

static
void
mms_decode_header_free_func(
    gpointer data,
    gpointer user_data)
{
    mms_header_free(data);
}

static
struct mms_headers*
mms_decode_headers(
    struct wsp_header_iter* iter,
    unsigned int flags,
    const char* (*well_known_header_name)(int hdr),
    mms_value_decoder (*value_decoder_for_header)(const char* hdr))
{
    struct mms_headers* headers;
    GPtrArray* tmp = g_ptr_array_new();
    while (wsp_header_iter_next(iter)) {
        const guint8* hdr = wsp_header_iter_get_hdr(iter);
        const char* hdr_name;

        switch (wsp_header_iter_get_hdr_type(iter)) {
        case WSP_HEADER_TYPE_WELL_KNOWN:
            hdr_name = well_known_header_name(hdr[0] & 0x7f);
            break;
        case WSP_HEADER_TYPE_APPLICATION:
            hdr_name = (const char*)hdr;
            break;
        default:
            hdr_name = NULL;
            break;
        }
        if (hdr_name) {
            const guint8* val = wsp_header_iter_get_val(iter);
            unsigned int val_len = wsp_header_iter_get_val_len(iter);
            enum wsp_value_type val_type = wsp_header_iter_get_val_type(iter);
            mms_value_decoder dec = value_decoder_for_header(hdr_name);
            struct mms_header* header = mms_header_new(hdr_name);
            g_ptr_array_add(tmp, header);
            if (!val_len ||
               (dec && dec(header, val_type, val, val_len, flags))) {
                continue;
            }
        }
        g_ptr_array_foreach(tmp, mms_decode_header_free_func, NULL);
        g_ptr_array_unref(tmp);
        return NULL;
    }
    headers = g_slice_new0(struct mms_headers);
    headers->num_headers = tmp->len;
    headers->headers = (struct mms_header**)g_ptr_array_free(tmp, FALSE);
    return headers;
}

static
struct mms_headers*
mms_message_decode_headers(
    struct wsp_header_iter* iter,
    unsigned int flags)
{
    return mms_decode_headers(iter, flags,
        mms_message_well_known_header_name,
        mms_message_value_decoder_for_header);
}

static
struct mms_headers*
mms_part_decode_headers(
    struct wsp_header_iter* iter,
    unsigned int flags)
{
    return mms_decode_headers(iter, flags,
        mms_part_well_known_header_name,
        mms_part_value_decoder_for_header);
}

static
void
mms_decode_dump_data(
    const unsigned char* data,
    unsigned int len)
{
    const unsigned int linelen = 16;
    unsigned int i;
    char line[80];
    for (i=0; i<len; i+=linelen) {
        unsigned int j;
        char tmp[8];
        sprintf(line,"  %04x: ", (unsigned int)i);
        for (j=i; j<(i+linelen); j++) {
            if ((j%16) == 8) strcat(line, " ");
            if (j < len) {
                sprintf(tmp, "%02x ", (unsigned int)data[j]);
                strcat(line, tmp);
            } else {
                strcat(line, "   ");
            }
        }
        strcat(line, "  ");
        for (j=i; j<(i+linelen); j++) {
            if (j<len) {
                sprintf(tmp, "%c", isprint(data[j]) ? data[j] : '.');
                strcat(line, tmp);
            } else {
                strcat(line, " ");
            }
        }
        printf("%s\n", line);
    }
}

static
void
mms_extract_attachment(
    int index,
    const struct mms_header* content_type,
    const struct mms_headers* headers,
    const void* data,
    unsigned int len)
{
    FILE* f;
    char* tmp = NULL;
    const char* filename = mms_headers_find_filename(content_type, headers);
    if (!filename) {
        if (index < 0) {
            filename = "attachment.dat";
        } else {
            filename = tmp = g_strdup_printf("attachment.%03d.dat", index);
        }
    }
    f = fopen(filename, "wb");
    if (f) {
        printf("Writing %s\n", filename);
        fwrite(data, 1, len, f);
        fclose(f);
    } else {
        printf("ERROR! Can't create %s\n", filename);
    }
    g_free(tmp);
}

static
gboolean
mms_decode_multipart(
    struct wsp_header_iter* iter,
    unsigned int flags)
{
    struct wsp_multipart_iter mi;
    if (wsp_multipart_iter_init(&mi, iter, NULL, NULL)) {
        int i;
        for (i=0; wsp_multipart_iter_next(&mi); i++) {
            unsigned int n;
            const void* type;
            const unsigned char* ct = wsp_multipart_iter_get_content_type(&mi);
            unsigned int ct_len = wsp_multipart_iter_get_content_type_len(&mi);
            if (wsp_decode_content_type(ct, ct_len, &type, &n, NULL)) {
                struct wsp_header_iter hi;
                const unsigned char* body = wsp_multipart_iter_get_body(&mi);
                unsigned int len = wsp_multipart_iter_get_body_len(&mi);
                unsigned int off = body - wsp_header_iter_get_pdu(iter);
                struct mms_header* ctype = mms_header_new_content_type();
                struct mms_headers* headers;
                const char* prefix = "  ";
                printf("Attachment #%d:\n", i+1);
                if (flags & MMS_DUMP_FLAG_VERBOSE) {
                    printf("Offset: %u (0x%x)\n", off, off);
                    printf("Length: %u (0x%x)\n", len, len);
                } else {
                    printf("Offset: %u\n", off);
                    printf("Length: %u\n", len);
                }
                ctype->value = g_strdup((char*)type);
                mms_value_decode_wsp_params(ctype, ct + n, ct_len - n);
                mms_header_dump(ctype, ct, ct_len, flags);
                mms_header_print(ctype, prefix, flags);
                wsp_header_iter_init(&hi, wsp_multipart_iter_get_hdr(&mi),
                    wsp_multipart_iter_get_hdr_len(&mi), 0);
                headers = mms_part_decode_headers(&hi, flags);
                mms_headers_print(headers, prefix, flags);
                if (flags & MMS_DUMP_FLAG_DATA) {
                    printf("Data:\n");
                    mms_decode_dump_data(body, len);
                }
                if (flags & MMS_DUMP_FLAG_EXTRACT) {
                    mms_extract_attachment(i+1, ctype, headers, body, len);
                }
                mms_header_free(ctype);
                mms_headers_free(headers);
                if (wsp_header_iter_at_end(&hi)) continue;
            }
            return FALSE;
        }
        return wsp_multipart_iter_close(&mi, iter);
    }
    return FALSE;
}

static
gboolean
mms_decode_attachment(
    struct wsp_header_iter* iter,
    unsigned int flags)
{
    const unsigned char* pdu = iter->pdu + iter->pos + 1;
    unsigned int len = iter->max - iter->pos - 1;
    unsigned int consumed, parlen;
    const void *type = NULL;
    if (wsp_decode_content_type(pdu, len, &type, &consumed, &parlen)) {
        struct mms_header* content_type = mms_header_new_content_type();
        unsigned int total = consumed + parlen;
        unsigned int off = iter->pos + 1 + total;
        const unsigned char* body = iter->pdu + off;
        len -= total;
        printf("Attachment:\n");
        if (flags & MMS_DUMP_FLAG_VERBOSE) {
            printf("Offset: %u (0x%x)\n", off, off);
            printf("Length: %u (0x%x)\n", len, len);
        } else {
            printf("Offset: %u\n", off);
            printf("Length: %u\n", len);
        }
        content_type->value = g_strdup((char*)type);
        mms_value_decode_wsp_params(content_type, pdu + consumed, parlen);
        mms_header_dump(content_type, iter->pdu + iter->pos, total+1, flags);
        mms_header_print(content_type, "  ", flags);
        if (flags & MMS_DUMP_FLAG_DATA) {
            printf("Data:\n");
            mms_decode_dump_data(body, len);
        }
        if (flags & MMS_DUMP_FLAG_EXTRACT) {
            mms_extract_attachment(-1, content_type, NULL, body, len);
        }
        mms_header_free(content_type);
        return TRUE;
    }
    return FALSE;
}

static
int
mms_decode_data(
    const guint8* data,
    gsize len,
    unsigned int flags)
{
    struct wsp_header_iter iter;
    struct mms_headers* headers;

    /* Skip WSP Push notification header if we find one */
    if (len >= 3 && data[1] == 6 /* Push PDU */) {
        unsigned int hdrlen = 0;
        unsigned int off = 0;
        const guint8* wsp_data = data+2;
        gsize wsp_len = len - 2;

        /* Hdrlen */
        if (wsp_decode_uintvar(wsp_data, wsp_len, &hdrlen, &off) &&
            (off + hdrlen) <= wsp_len) {
            const void* ct = NULL;
            wsp_data += off;
            wsp_len -= off;
            if (wsp_decode_content_type(wsp_data, hdrlen, &ct, &off, NULL) &&
                strcmp(ct, MMS_CONTENT_TYPE) == 0) {
                printf("WSP header:\n  %s\n", (char*)ct);
                data = wsp_data + hdrlen;
                len = wsp_len - hdrlen;
            }
        }
    }

    printf("MMS headers:\n");
    wsp_header_iter_init(&iter, data, len, WSP_HEADER_ITER_FLAG_REJECT_CP |
        WSP_HEADER_ITER_FLAG_DETECT_MMS_MULTIPART);
    headers = mms_message_decode_headers(&iter, flags);
    if (headers) {
        mms_headers_print(headers, "  ", flags);
        mms_headers_free(headers);
        if (wsp_header_iter_at_end(&iter)) {
            return RET_OK;
        } else if (wsp_header_iter_is_content_type(&iter)) {
            if (wsp_header_iter_is_multipart(&iter)) {
                if (mms_decode_multipart(&iter, flags) &&
                    wsp_header_iter_at_end(&iter)) {
                    return RET_OK;
                }
            } else if (mms_decode_attachment(&iter, flags)) {
                return RET_OK;
            }
        }
    }
    printf("Decoding FAILED\n");
    return RET_ERR_DECODE;
}

static
int
mms_decode_file(
    const char* fname,
    unsigned int flags)
{
    int ret = RET_ERR_IO;
    GError* error = NULL;
    GMappedFile* map = g_mapped_file_new(fname, FALSE, &error);
    if (map) {
        const void* data = g_mapped_file_get_contents(map);
        const gsize size = g_mapped_file_get_length(map);
        ret = mms_decode_data(data, size, flags);
        g_mapped_file_unref(map);
    } else {
        printf("%s: %s\n", pname, error->message);
        g_error_free(error);
    }
    return ret;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR_CMDLINE;
    gboolean ok, verbose = FALSE, data = FALSE, extract = FALSE;
    GError* error = NULL;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "data", 'd', 0, G_OPTION_ARG_NONE, &data,
          "Dump attachment data", NULL },
        { "extract", 'x', 0, G_OPTION_ARG_NONE, &extract,
          "Extract attachments", NULL },
        { NULL }
    };
    GOptionContext* options = g_option_context_new("FILES");
    g_option_context_add_main_entries(options, entries, NULL);
    ok = g_option_context_parse(options, &argc, &argv, &error);
    if (ok) {
        if (argc > 1) {
            int i, flags = 0;
            if (verbose) flags |= MMS_DUMP_FLAG_VERBOSE;
            if (extract) flags |= MMS_DUMP_FLAG_EXTRACT;
            if (data) flags |= MMS_DUMP_FLAG_DATA;
            for (i=1; i<argc; i++) {
                const char* fname = argv[i];
                if (argc > 2) printf("\n%s\n\n", fname);
                ret = mms_decode_file(fname, flags);
                if (ret != RET_OK) break;
            }
        } else {
            char* help = g_option_context_get_help(options, TRUE, NULL);
            printf("%s", help);
            g_free(help);
        } 
    } else {
        printf("%s: %s\n", pname, error->message);
        g_error_free(error);
    }
    g_option_context_free(options);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
