/*
/*
 * libstomp - a free implementation of the stomp protocol than can be plugged
 * to different connection implementations using an adapter interface.
 *
 * 1 adapters available:
 *
 *  * websockets: make connection to Websockets via libwebsockets.
 *
 * https://stomp.github.io/
 * https://github.com/warmcat/libwebsockets
 *
 * Copyright (C) 2017 Sergio Otero <sergio.otero@gmail.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#ifndef libstomp_H
#define libstomp_H

#include <time.h>

extern const int STOMP_DEBUG;

#define stomp_debug_print(fmt, ...) \
		do { if (STOMP_DEBUG) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

#define STOMP_MAX_FRAME_BUFFER 2048

enum StompAdapterStatus {
	created,
	initialized,
	preconnected,
	connected,
	disconnected,
	destroyed
};

typedef struct {
  char *name;
  char *value;
} StompHeader;

typedef struct {
  size_t len;
  StompHeader *header_array;
} StompHeaders;

typedef struct {
  char *command;
  StompHeaders *system_headers;
  StompHeaders *user_headers;
  char *body;
} StompFrame;

typedef struct StompInfo StompInfo;
typedef struct StompSubscription StompSubscription;


typedef void (*stomp_callback)(StompInfo *stomp_info, const StompFrame *frame);

struct StompSubscription{
  char subscription_id[30]; //TODO length?
  stomp_callback message_callback;
  StompSubscription *previous;
  StompSubscription *next;
};

typedef struct StompAdapter StompAdapter;

typedef int (*stomp_adapter_init_function)(StompAdapter *adapter, StompAdapter *parent_adapter);
typedef int (*stomp_adapter_service_function)(StompAdapter *adapter, int timeout_ms);
typedef int (*stomp_adapter_connect_function)(StompAdapter *adapter);
typedef int (*stomp_adapter_send_function)(StompAdapter *adapter, char *message);
typedef int (*stomp_adapter_restart_function)(StompAdapter *adapter);
typedef int (*stomp_adapter_destroy_function)(StompAdapter *adapter);

typedef int (*stomp_adapter_onopen_callback)(StompAdapter *adapter);
typedef int (*stomp_adapter_onmessage_callback)(StompAdapter *adapter, char *message);
typedef int (*stomp_adapter_onerror_callback)(StompAdapter *adapter, char *message);
typedef int (*stomp_adapter_onheartbeat_callback)(StompAdapter *adapter);
typedef int (*stomp_adapter_onclose_callback)(StompAdapter *adapter, char *message);

struct StompAdapter{
	enum StompAdapterStatus status;
	stomp_adapter_init_function init_function;
	stomp_adapter_connect_function connect_function;
	stomp_adapter_send_function send_function;
	stomp_adapter_service_function service_function;
	stomp_adapter_restart_function restart_function;
	stomp_adapter_destroy_function destroy_function;

	stomp_adapter_onopen_callback onopen_callback;
	stomp_adapter_onmessage_callback onmessage_callback;
	stomp_adapter_onerror_callback onerror_callback;
	stomp_adapter_onheartbeat_callback onheartbeat_callback;
	stomp_adapter_onclose_callback onclose_callback;

	StompAdapter *parent_adapter;
	StompAdapter *child_adapter;

	void *custom_data;
};


struct StompInfo {
	StompAdapter adapter;
	StompHeaders connect_headers;

	stomp_callback connect_callback;
	stomp_callback error_callback;

	int next_subscription_id;
	StompSubscription *subscriptions;

	time_t last_server_action_time;
	void *custom_data;
};

extern StompAdapter stomp_libwebsockets_adapter(char *url);

extern StompInfo stomp_create(StompAdapter *adapter);

extern int stomp_init(StompInfo *stomp_info);

extern int stomp_connect(StompInfo *stomp_info, StompHeaders *headers, stomp_callback connect_callback, stomp_callback error_callback);

extern int stomp_reconnect(StompInfo *stomp_info);

extern StompHeader* stomp_find_header(StompHeaders *headers, char *name);

extern char* stomp_subscribe(StompInfo *stomp_info, char *destination, stomp_callback message_callback, StompHeaders* headers);

extern int stomp_unsubscribe(StompInfo *stomp_info, char *subscription_id);

extern int stomp_service(StompInfo *stomp_info, int timeout_ms);

extern int stomp_destroy(StompInfo *stomp_info);

extern StompAdapter stomp_libwebsockets_adapter(char *url);

extern void stomp_frame_marshall(const StompFrame *frame, char *buffer, int maxLength);

#endif
