/** @file
    REST (HTTP) output for rtl_433 events

    Copyright (C) 2020 Ville Hukkamäki

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "output_http.h"
#include "fatal.h"

#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "abuf.h"



typedef struct {
    CURL *curl;
    struct curl_slist *headers;
    FILE *output;
    char *url;
} rest_client_t;

static int rest_client_open(rest_client_t *client, const char *url, int header_count, char *headers[])
{
    if (!url)
        return -1;

    curl_global_init(CURL_GLOBAL_ALL);
    client->curl=curl_easy_init();
    if (!client->curl) {
        return -1;
    }

    client->url=strdup(url);
    if (!client->url) {
        WARN_STRDUP("rest_client_open()");
        return -1;
    }

    client->output=fopen("/dev/null","w+");

    for (int i=0; i < header_count; i++) {
        client->headers = curl_slist_append(client->headers, headers[i]);
    }

    client->headers = curl_slist_append(client->headers, "Content-type: application/json");

    return 0;
}

static void rest_client_send(rest_client_t *client, const char *message, size_t message_len)
{
    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_URL, client->url);
    curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, client->headers);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA,client->output);
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, message_len);
    curl_easy_setopt(client->curl, CURLOPT_COPYPOSTFIELDS, message);
    CURLcode res = curl_easy_perform(client->curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "WARNING: REST failed: %s\n",
                curl_easy_strerror(res));
        return;
    }
}

typedef struct {
    struct data_output output;
    rest_client_t client;
} data_output_rest_t;

static void print_rest_data(data_output_t *output, data_t *data, char const *format)
{
    data_output_rest_t *rest = (data_output_rest_t *)output;

    char message[16384];
    abuf_t msg = {0};
    abuf_init(&msg, message, sizeof(message));

    msg.tail += data_print_jsons(data, msg.tail, msg.left);
    if (msg.tail >= msg.head + sizeof(message))
        return;

    size_t abuf_len = msg.tail - msg.head;
    rest_client_send(&rest->client, message, abuf_len);
}

static void data_output_rest_free(data_output_t *output)
{
    data_output_rest_t *rest = (data_output_rest_t *)output;

    if (!rest)
        return;

    free(rest->client.url);
    curl_slist_free_all(rest->client.headers);
    curl_easy_cleanup(rest->client.curl);
    curl_global_cleanup();

    free(rest);
}

struct data_output *data_output_rest_create(const char *url, int header_count, char *headers[])
{
    data_output_rest_t *rest = calloc(1, sizeof(data_output_rest_t));
    if (!rest) {
        WARN_CALLOC("data_output_rest_create()");
        return NULL;
    }

    rest->output.print_data   = print_rest_data;
    rest->output.output_free  = data_output_rest_free;
    rest_client_open(&rest->client, url, header_count, headers);

    return &rest->output;
}
