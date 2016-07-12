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

#include "test_http.h"

#include <gutil_log.h>

#include <libsoup/soup.h>

/* A single HTTP response */
typedef struct test_http_response {
    GMappedFile* file;
    char* content_type;
    int status;
} TestHttpResponse;

struct test_http {
    gint ref_count;
    guint port;
    SoupServer* server;
    GPtrArray* responses;
    GPtrArray* post_data;
    gboolean disconnected;
    guint current_resp;
};

static
void
test_http_post_data_free(
    gpointer data)
{
    soup_buffer_free((SoupBuffer*)data);
}

static
void
test_http_callback(
    SoupServer* server,
    SoupMessage* msg,
    const char* path,
    GHashTable* query,
    SoupClientContext* context,
    gpointer data)
{
    TestHttp* http = data;
    char* uri = soup_uri_to_string(soup_message_get_uri (msg), FALSE);
    GVERBOSE("%s %s HTTP/1.%d", msg->method, uri,
        soup_message_get_http_version(msg));
    g_free(uri);
    if (msg->method == SOUP_METHOD_CONNECT) {
        soup_message_set_status(msg, SOUP_STATUS_NOT_IMPLEMENTED);
    } else {
        if (msg->request_body->length) {
            SoupBuffer* post = soup_message_body_flatten(msg->request_body);
            g_ptr_array_add(http->post_data,
                 g_bytes_new_with_free_func(post->data, post->length,
                     test_http_post_data_free, post));
        }
        if (http->current_resp >= http->responses->len) {
            soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
        } else {
            const TestHttpResponse* resp =
                http->responses->pdata[(http->current_resp)++];
            soup_message_set_status(msg, resp->status);
            soup_message_headers_set_content_type(msg->response_headers,
                resp->content_type ? resp->content_type : "text/plain", NULL);
            soup_message_headers_append(msg->response_headers,
                "Accept-Ranges", "bytes");
            soup_message_headers_append(msg->response_headers,
                "Connection", "close");
            if (resp->file) {
                soup_message_headers_set_content_length(msg->response_headers,
                    g_mapped_file_get_length(resp->file));
                soup_message_body_append(msg->response_body,
                    SOUP_MEMORY_TEMPORARY,
                    g_mapped_file_get_contents(resp->file),
                    g_mapped_file_get_length(resp->file));
            } else {
                soup_message_headers_set_content_length(msg->response_headers,0);
            }
        }
    }
    soup_message_body_complete(msg->request_body);
}

guint
test_http_get_port(
    TestHttp* http)
{
    return http->port;
}

guint
test_http_get_post_count(
    TestHttp* http)
{
    return http ? http->post_data->len : 0;
}

GBytes*
test_http_get_post_data_at(
    TestHttp* http,
    guint index)
{
    return (http && index < http->post_data->len) ?
        http->post_data->pdata[index] : NULL;
}

GBytes*
test_http_get_post_data(
    TestHttp* http)
{
    return (http && http->post_data->len) ? http->post_data->pdata[0] : NULL;
}

void
test_http_close(
    TestHttp* http)
{
    if (http && !http->disconnected) {
        http->disconnected = TRUE;
        soup_server_disconnect(http->server);
    }
}

static
void
test_http_post_data_bytes_free(
    gpointer data)
{
    g_bytes_unref(data);
}

static
void
test_http_response_free(
    gpointer data)
{
    TestHttpResponse* resp = data;
    if (resp->file) {
        g_mapped_file_unref(resp->file);
        g_free(resp->content_type);
    }
    g_free(resp);
}

void
test_http_add_response(
    TestHttp* http,
    GMappedFile* file,
    const char* content_type,
    int status)
{
    TestHttpResponse* resp = g_new0(TestHttpResponse, 1);
    if (file) {
        resp->file = g_mapped_file_ref(file);
        resp->content_type = g_strdup(content_type);
    }
    resp->status = status;
    g_ptr_array_add(http->responses, resp);
}

#if SOUP_CHECK_VERSION(2,48,0)
static
void
test_http_uri_free(
    gpointer uri)
{
    soup_uri_free(uri);
}
#endif

TestHttp*
test_http_new(
    GMappedFile* get_file,
    const char* resp_content_type,
    int resp_status)
{
    TestHttp* http = g_new0(TestHttp, 1);
    http->ref_count = 1;
    http->responses = g_ptr_array_new_full(0, test_http_response_free);
    http->post_data = g_ptr_array_new_full(0, test_http_post_data_bytes_free);
    http->server = g_object_new(SOUP_TYPE_SERVER, NULL);
#if SOUP_CHECK_VERSION(2,48,0)
    if (soup_server_listen_local(http->server, 0, 0, NULL)) {
        GSList* uris = soup_server_get_uris(http->server);
        if (uris) {
            SoupURI* uri = uris->data;
            http->port = soup_uri_get_port(uri);
            g_slist_free_full(uris, test_http_uri_free);
        }
    }
#else
    http->port = soup_server_get_port(http->server);
    soup_server_run_async(http->server);
#endif
    GDEBUG("Listening on port %hu", http->port);
    soup_server_add_handler(http->server, NULL, test_http_callback, http, NULL);
    if (get_file || resp_content_type || resp_status) {
        test_http_add_response(http, get_file, resp_content_type, resp_status);
    }
    return http;
}

TestHttp*
test_http_ref(
    TestHttp* http)
{
    if (http) {
        GASSERT(http->ref_count > 0);
        g_atomic_int_inc(&http->ref_count);
    }
    return http;
}

void
test_http_unref(
    TestHttp* http)
{
    if (http) {
        GASSERT(http->ref_count > 0);
        if (g_atomic_int_dec_and_test(&http->ref_count)) {
            test_http_close(http);
            g_ptr_array_unref(http->responses);
            g_ptr_array_unref(http->post_data);
            g_object_unref(http->server);
            g_free(http);
        }
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
