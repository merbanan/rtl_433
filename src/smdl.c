/*
Copyright (C) 2018 Benjamin Larsson <banan@ludd.ltu.se>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

/**
 * Syncronous message distribution layer
 * 
 * MQTT protocol supported
 * 
 */


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <malloc.h>
#include <time.h>
#include <inttypes.h>
#include "data.h"

#include "limits.h"
// gethostname() needs _XOPEN_SOURCE 500 on unistd.h
#define _XOPEN_SOURCE 500

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
  #if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
  #undef _WIN32_WINNT
  #define _WIN32_WINNT 0x0600   /* Needed to pull in 'struct sockaddr_storage' */
  #endif

  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h> 
  #include <unistd.h>

  #define SOCKET          int
  #define INVALID_SOCKET  -1
#endif


#define JSON_BUFFER_SIZE 4096

typedef struct {
    int initialized;
    char server[256];
    char port[256];
    char json_buf[JSON_BUFFER_SIZE];
} smdl_ctx;


static smdl_ctx* q_ctx;

smdl_ctx* smdl_allocate() {
    smdl_ctx* q;
    q = malloc(sizeof(smdl_ctx));
    memset(q, 0, sizeof(smdl_ctx));
    q->initialized = 1;
    return q;
}

int smdl_deallocate(smdl_ctx* q) {
    free(q);
    return(0);
}

typedef struct {
    struct sockaddr_storage addr;
    socklen_t addr_len;
    SOCKET sock;
    int sockfd;
} datagram_client_t;

typedef struct {
    struct data_output output;
    datagram_client_t client;
    char json_buf[JSON_BUFFER_SIZE];
    int pri;
    char hostname[_POSIX_HOST_NAME_MAX + 1];
} data_output_smdl_t;

#define CONNECT_BUF_SIZE        1024+JSON_BUFFER_SIZE
#define MQTT_PROTOCOL_CONNECT   0x1
#define MQTT_PROTOCOL_PUBLISH   0x3
#define MQTT_PROTOCOL_NAME      "MQTT"
#define MQTT_PROTOCOL_LEVEL     0x04
#define MQTT_CLEAN_SESSION_FLAG 0x02
#define MQTT_CLIENT_IDENTIFIER  "rtl_433 smdl 1.0"
#define MQTT_CLIENT_DEFAULT_TOPIC  "home/rtl_433/"
//#define MQTT_CLIENT_DEFAULT_TOPIC  "a/b"



void smdl_encode_16b(unsigned char *cb, uint16_t value) {
    cb[0] = (value & 0xFF00) >> 8;
    cb[1] = value & 0x00FF;
}

int smdl_set_fixed(uint8_t *cb, uint8_t pkt_type, uint8_t flags, uint8_t remaining_length) {
    int encoded_byte = 0;
    int x = remaining_length;
    int idx = 1;
    cb[0] = (pkt_type << 4) | flags;

    do {
        encoded_byte = x % 128;
        x = x / 128;
        if (x > 0)
            encoded_byte = encoded_byte | 128;
        cb[idx] = encoded_byte;
        idx++;
    } while (x > 0);

    return remaining_length + idx;
}

int smdl_set_connect_variable(uint8_t *cb) {
    smdl_encode_16b(cb, 4);
    memcpy(&cb[2], MQTT_PROTOCOL_NAME, 4); 
    cb[6] = MQTT_PROTOCOL_LEVEL;
    cb[7] = MQTT_CLEAN_SESSION_FLAG;
    smdl_encode_16b(&cb[8], 0);     // Keep alive
    smdl_encode_16b(&cb[10], strlen(MQTT_CLIENT_IDENTIFIER));   // Identifier
    memcpy(&cb[12], MQTT_CLIENT_IDENTIFIER, strlen(MQTT_CLIENT_IDENTIFIER));
    return 12 + strlen(MQTT_CLIENT_IDENTIFIER);
}

const char mqtt_connect[15] = { 0x10, 0x0D,
    0x00, 0x04,
    'M', 'Q', 'T', 'T',
    0x04,
    0x02,
    0x00, 0x00,
    0x00, 0x01,
    'A' };

static int datagram_client_open(datagram_client_t *client, const char *host, const char *port)
{

    struct addrinfo hints, *res, *res0;
    int    error;
    SOCKET sock;
    const char *cause = NULL;
    
    int sockfd, numbytes;
    struct hostent *he;
    struct sockaddr_in d_addr;
    uint8_t conn_buf[CONNECT_BUF_SIZE] = {0};
    uint8_t ans_buf[CONNECT_BUF_SIZE] = {0};
    int pkt_len, buf_idx = 0;

    if (!host || !port)
        return -1;

    if ((he=gethostbyname(host)) == NULL) {  /* get the host info */
        perror("gethostbyname");
        exit(1);
    }

    if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    client->sockfd = sockfd;
    
    d_addr.sin_family = AF_INET;      /* host byte order */
    d_addr.sin_port = htons(atoi(port));    /* short, network byte order */
    d_addr.sin_addr = *((struct in_addr *)he->h_addr_list[0]);

    /* Generate connect payload */
    buf_idx = smdl_set_connect_variable(&conn_buf[2]);
    pkt_len = smdl_set_fixed(conn_buf, MQTT_PROTOCOL_CONNECT, 0x0, buf_idx);

    /* Create socket for server connection */
    if (connect(sockfd, (struct sockaddr *)&d_addr, sizeof(struct sockaddr)) == -1) {
        perror("connect");
        exit(1);
    }

    /* Send connect packet */
    if (send(sockfd, conn_buf, pkt_len, 0) == -1){
        perror("send");
        exit (1);
    }

    /* Get answer (unimplemented) */
    if ((numbytes=recv(sockfd, ans_buf, CONNECT_BUF_SIZE, 0)) == -1) {
        perror("recv");
        exit(1);
    }

    return 0;
    
    #define MAXDATASIZE 100
    char buf[MAXDATASIZE];
    while (1) {
		if (send(sockfd, "Hello, world!\n", 14, 0) == -1){
                      perror("send");
		      exit (1);
		}
		printf("After the send function \n");

        	if ((numbytes=recv(sockfd, buf, MAXDATASIZE, 0)) == -1) {
            		perror("recv");
            		exit(1);
		}	

	        buf[numbytes] = '\0';

        	printf("Received in pid=%d, text=: %s \n",getpid(), buf);
		sleep(1);

	}
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol=0;
    hints.ai_flags = AI_ADDRCONFIG;
    error = getaddrinfo(host, port, &hints, &res0);
    if (error) {
        fprintf(stderr, "%s\n", gai_strerror(error));
        return -1;
    }
    sock = INVALID_SOCKET;
    for (res = res0; res; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock >= 0) {
            client->sock = sock;
            memset(&client->addr, 0, sizeof(client->addr));
            memcpy(&client->addr, res->ai_addr, res->ai_addrlen);
            client->addr_len = res->ai_addrlen;
            break; // success
        }
    }
    freeaddrinfo(res0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return -1;
    }

    fprintf(stderr, "Opening socket connection\n");
    // connect(sock, (struct sockaddr*) &sockaddrin, sizeof(struct sockaddr_in));
    //int broadcast = 1;
    //int ret = setsockopt(client->sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    return 0;
}

int smdl_connect(smdl_ctx* q, const char *host, const char *port) {
    strcpy(q->server, host);
    strcpy(q->port, port);
    
    
    return 0;
}

int smdl_ping(smdl_ctx* q) {
    return 0;
}

int smdl_transmit(smdl_ctx* q) {
    return 0;
}

void data_output_smdl_free(data_output_t *output)
{
    if (!output)
        return;

    fprintf(stderr, "data_output_smdl_free\n");

    free(output);
}

int smdl_set_publish_variable(uint8_t *cb, char* subtopic, char* payload) {
    int idx = 0, ret;
    // Topic bytes
    smdl_encode_16b(&cb[idx], strlen(MQTT_CLIENT_DEFAULT_TOPIC)+strlen(subtopic));
    idx+=2;
    // Topic
    memcpy(&cb[idx], MQTT_CLIENT_DEFAULT_TOPIC, strlen(MQTT_CLIENT_DEFAULT_TOPIC));
    idx+=strlen(MQTT_CLIENT_DEFAULT_TOPIC);
    memcpy(&cb[idx], subtopic, strlen(subtopic));
    idx+=strlen(subtopic);
    //Payload
    memcpy(&cb[idx], payload, strlen(payload));
    idx+=strlen(payload);
    return idx;
}


void smdl_generate_output(data_output_t *output, data_t *data, char *format) {
    //incomplete error handling
    data_output_smdl_t *smdl = (data_output_smdl_t *)output;
    data_t* model_ptr = data;
    int pkt_len, buf_idx = 0;
    uint8_t conn_buf[CONNECT_BUF_SIZE] = {0};
    uint8_t ans_buf[CONNECT_BUF_SIZE] = {0};
    char payload[2] = {0};
    payload[0] = (char) 0xF8;
    payload[1] = (char) 0xF2;

    print_json_data(output, data, format);
    fprintf(stderr, "smdl |%s|\n", smdl->json_buf);

    // Find (sub)topic string
    while (strcmp(model_ptr->key, "model")) {
        model_ptr = data->next;
    }
    fprintf(stderr, "smdl: %s %d \n", (char*)model_ptr->value, strlen(smdl->json_buf));
    smdl->json_buf[85] = '\0';
    buf_idx = smdl_set_publish_variable(&conn_buf[2], (char*)model_ptr->value, smdl->json_buf);
    pkt_len = smdl_set_fixed(conn_buf, MQTT_PROTOCOL_PUBLISH, 0x0, buf_idx);

    /* Send publish packet */
    if (send(smdl->client.sockfd, conn_buf, pkt_len, 0) == -1){
        perror("send");
        exit (1);
    }
    fprintf(stderr, "publish packet sent\n");
    return;
}

struct data_output *data_output_smdl_create(const char *host, const char *port)
{
    data_output_smdl_t *smdl = calloc(1, sizeof(data_output_smdl_t));
    if (!smdl) {
        fprintf(stderr, "calloc() failed");
        return NULL;
    }

//    q_ctx = smdl_allocate();
//    smdl_connect(q_ctx, host, port);
//    datagram_client_open(q_ctx->client, host, port);

    smdl->output.print_data   = smdl_generate_output;
    smdl->output.print_array  = print_json_array;
    smdl->output.print_string = print_json_string;
    smdl->output.print_double = print_json_double;
    smdl->output.print_int    = print_json_int;
    smdl->output.output_free  = data_output_smdl_free;
    smdl->output.file         = fmemopen(smdl->json_buf, JSON_BUFFER_SIZE, "r+");
    // Severity 5 "Notice", Facility 20 "local use 4"
    smdl->pri = 20 * 8 + 5;
    gethostname(smdl->hostname, _POSIX_HOST_NAME_MAX + 1);
    smdl->hostname[_POSIX_HOST_NAME_MAX] = '\0';
    datagram_client_open(&smdl->client, host, port);

    // unbuffered output
    setbuf(smdl->output.file,NULL);
    fprintf(stderr, "smdl setup\n");

    return &smdl->output;
}
