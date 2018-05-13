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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//
//#include <syslog.h>
//#include <time.h>
//#include <unistd.h>

#include "libstomp.h"

const int STOMP_DEBUG = 0;

typedef struct {
	StompInfo *stomp_info;
} StompAdapterStompInfo;

static StompAdapterStompInfo* get_adapter_custom_data(StompAdapter *adapter) {
	return (StompAdapterStompInfo*)adapter->custom_data;
}

StompHeader* stomp_find_header(StompHeaders *headers, char *name) {
	if (headers == NULL) return NULL;

	for (int i = 0; i < headers->len; i++) {
		if (!strcmp(headers->header_array[i].name, name)) return &headers->header_array[i];
	}

	return NULL;
}

StompSubscription* stomp_find_subscription(StompInfo *stomp_info, char *subscription_id) {
	StompSubscription *current = stomp_info->subscriptions;
	while (current != NULL) {
		if (!strcmp(current->subscription_id, subscription_id)) return current;
		current = current->next;
	}

	return NULL;
}

int stomp_frame_header_marshall(StompHeaders *headers, char *buffer, int skipContentLength) {
	if (headers) {
		for (int i = 0; i < headers->len; i++) {
			StompHeader *header = &headers->header_array[i];

			if (!strcmp(header->name, "content-length")) {
				skipContentLength = !strcmp(header->value, "false");
				continue;
			}
			strcat(buffer, header->name);
			strcat(buffer, ":");
			strcat(buffer, header->value);
			strcat(buffer, "\n");
		}
	}

	return skipContentLength;
}

void stomp_frame_marshall(const StompFrame *frame, char *buffer, int maxLength) {
	//TODO respect maxlength

	strcpy(buffer, frame->command);
	strcat(buffer, "\n");

	int skipContentLength = stomp_frame_header_marshall(frame->system_headers, buffer, 0);
		skipContentLength = stomp_frame_header_marshall(frame->user_headers, buffer, skipContentLength);

	if (frame->body && !skipContentLength) {
		char temp[25];
		//FIXME it should be utf-8 length, not byte length
		sprintf(temp,"content-length:%lu\n", strlen(frame->body));
		strcat(buffer, temp);
	}

	strcat(buffer, "\n");

	if (frame->body) {
		strcat(buffer, frame->body);
	}

//	strcat(buffer, "\0");
}

int stomp_transmit(StompInfo *stomp_info, StompFrame *frame) {
	StompAdapter *child_adapter = stomp_info->adapter.child_adapter;

	int max_frame_length = stomp_info->adapter.max_frame_length;

	//TODO reuse buffer
    char message[max_frame_length];

    stomp_frame_marshall(frame, message, max_frame_length);

	stomp_debug_print("stomp sending:\n%s\n", message);

	return child_adapter->send_function(child_adapter, message);
}

int stomp_send_connect(StompInfo *stomp_info) {
	//TODO headers
	StompHeader system_headers_array[2];
	system_headers_array[0].name = "accept-version";
	system_headers_array[0].value = "1.1,1.0";
	system_headers_array[1].name = "heart-beat";
	system_headers_array[1].value = "10000,10000";

	StompHeaders system_headers = {.len = 2 , .header_array = system_headers_array};
	StompFrame frame = {.command = "CONNECT", .system_headers = &system_headers, .user_headers = &stomp_info->connect_headers, .body = NULL};

	return stomp_transmit(stomp_info, &frame);
}

char* stomp_read_line(char *cur_line) {
  char *next_line = strstr(cur_line, "\n");
  if (next_line) {
	  *next_line = '\0';  // temporarily terminate the current line
	  return &next_line[1];
  } else {
	  return NULL;
  }
}

void stomp_empty_frame(StompFrame *frame) {
	frame->command = NULL;
	frame->body = NULL;
	frame->system_headers = NULL;
	frame->user_headers = NULL;
}

int stomp_frame_unmarshall(char *message, StompFrame *frame) {
	// read command
	char *cur_line = message;
	char *next_line = stomp_read_line(cur_line);
	frame->command = cur_line;

	StompHeaders *headers = malloc(sizeof(StompHeaders));
	headers->header_array = malloc(10*sizeof(StompHeader)); //TODO dynamic
	headers->len = 0;

	// read headers until newline
	while (next_line != NULL) {
		cur_line = next_line;
		next_line = stomp_read_line(cur_line);

		if (strlen(cur_line) == 0) break;

		char *sep = strchr(cur_line, ':');
		if (sep == NULL) return -1;

		// Separate string in 2 with NULL char
		*sep = 0;

		StompHeader *header = &headers->header_array[headers->len];
		header->name = cur_line;
		header->value = &sep[1];

		headers->len++;
	}

	frame->system_headers = headers;

	// read body
	frame->body = next_line;

	return 0;
}

int stomp_unmarshall_frame_free(StompFrame *frame) {
	if (frame->system_headers != NULL) {
		free(frame->system_headers->header_array);
		free(frame->system_headers);
	}

	if (frame->user_headers != NULL) {
		free(frame->user_headers->header_array);
		free(frame->user_headers);
	}

	return 0;
}

StompHeaders *stomp_prepare_headers(StompHeaders* system_headers, int system_headers_len, StompHeaders* user_headers) {
	int user_headers_len = user_headers == NULL ? 0 : user_headers->len;
	int total_len = system_headers_len + user_headers_len;

	if (total_len != system_headers->len) {
		return NULL;
	}

	memcpy(&system_headers[system_headers_len], user_headers->header_array, user_headers_len * sizeof(StompHeader));

	return system_headers;
}

void stomp_prepare_connect_headers(StompInfo *stomp_info, StompHeaders* connect_user_headers) {
  // store a copy of the user headers
  StompHeaders *connect_headers = &stomp_info->connect_headers;

  connect_headers->len = connect_user_headers->len;

  if (connect_headers->len > 0 && connect_headers != connect_user_headers) {
	  connect_headers->header_array = malloc(sizeof(StompHeader) * connect_headers->len);
	  memcpy(connect_headers->header_array, connect_user_headers->header_array, connect_headers->len * sizeof(StompHeader));
  }
}

int stomp_connect(StompInfo *stomp_info, StompHeaders *headers, stomp_callback connect_callback, stomp_callback error_callback) {
	if (stomp_info->adapter.status != initialized) return -1;

	StompAdapter *child_adapter = stomp_info->adapter.child_adapter;

	stomp_info->connect_callback = connect_callback;
	stomp_info->error_callback = error_callback;

	stomp_prepare_connect_headers(stomp_info, headers);

	stomp_info->adapter.status = preconnected;

	return child_adapter->connect_function(child_adapter);
}

int stomp_unsubscribe(StompInfo *stomp_info, char *subscription_id) {
	if (stomp_info->adapter.status != connected) return -1;

	StompSubscription *subscription = stomp_find_subscription(stomp_info, subscription_id);

	if (subscription == NULL) return -1;

	if (subscription->previous == NULL) {
		stomp_info->subscriptions = subscription->next;
	}
	if (subscription->next != NULL) {
		subscription->next->previous = subscription->previous;
	}

	free(subscription);

	StompHeader system_headers_array[1];
	system_headers_array[0].name = "id";
	system_headers_array[0].value = subscription_id;

	StompHeaders system_headers = {.len = 1 , .header_array = system_headers_array};
	StompFrame frame;
	stomp_empty_frame(&frame);
	frame.command = "UNSUBSCRIBE";
	frame.system_headers = &system_headers;

	return stomp_transmit(stomp_info, &frame);
}

char* stomp_subscribe(StompInfo *stomp_info, char *destination, stomp_callback message_callback, StompHeaders* headers) {
	if (stomp_info->adapter.status != connected) return NULL;

	StompSubscription *subscription = malloc(sizeof(StompSubscription));
	subscription->message_callback = message_callback;

	StompHeader *header_id = stomp_find_header(headers, "id");
	int num_headers = header_id ? 1 : 2;

	StompHeader system_headers_array[num_headers];
	system_headers_array[0].name = "destination";
	system_headers_array[0].value = destination;
	if (header_id) {
		// The client has specicified a suscription id
		strcpy(subscription->subscription_id, header_id->value);
	} else {
		// Autogenerate a suscription id
		sprintf(subscription->subscription_id, "sub-%d", stomp_info->next_subscription_id++);

		system_headers_array[1].name = "id";
		system_headers_array[1].value = subscription->subscription_id;
	}

	StompHeaders system_headers = {.len = num_headers , .header_array = system_headers_array};
	StompFrame frame = {.command = "SUBSCRIBE", .system_headers = &system_headers, .user_headers = headers, .body = NULL};

	if (stomp_transmit(stomp_info, &frame)) {
		free(subscription);
		return NULL;
	}

	subscription->next = stomp_info->subscriptions;
	subscription->previous = NULL;
	stomp_info->subscriptions = subscription;
	if (subscription->next != NULL) subscription->next->previous = subscription;

	return subscription->subscription_id;
}

int stomp_send(StompInfo *stomp_info, char *destination, StompHeaders* headers, char *message) {
	if (stomp_info->adapter.status != connected) return -1;

	StompHeader system_headers_array[1];
	system_headers_array[0].name = "destination";
	system_headers_array[0].value = destination;

	StompHeaders system_headers = {.len = 1 , .header_array = system_headers_array};
	StompFrame frame = {.command = "SEND", .system_headers = &system_headers, .user_headers = headers, .body = message};

	return stomp_transmit(stomp_info, &frame);
}

int stomp_service(StompInfo *stomp_info, int timeout_ms) {
	if (stomp_info->adapter.status != preconnected && stomp_info->adapter.status != connected) return -1;

	StompAdapter *child_adapter = stomp_info->adapter.child_adapter;

	return child_adapter->service_function(child_adapter, timeout_ms);
}

int stomp_destroy_internal(StompInfo *stomp_info, int reconnect) {
	if (stomp_info->adapter.status == destroyed) return -1;

	StompAdapter *adapter = &stomp_info->adapter;
	StompAdapter *child_adapter = adapter->child_adapter;

	StompSubscription *subscription = stomp_info->subscriptions;
	while (subscription != NULL) {
		StompSubscription *next_subscription = subscription->next;
		free(subscription);
		subscription = next_subscription;
	}

	stomp_info->subscriptions = NULL;

	if (reconnect) {
		child_adapter->restart_function(child_adapter);

		stomp_info->adapter.status = initialized;
	} else {
		child_adapter->destroy_function(child_adapter);
		free(adapter->custom_data);

		if (stomp_info->connect_headers.len > 0) {
			free(stomp_info->connect_headers.header_array);
		}

		stomp_info->adapter.status = destroyed;
	}


	return 0;
}

int stomp_destroy(StompInfo *stomp_info) {
	return stomp_destroy_internal(stomp_info, 0);
}

int stomp_reconnect(StompInfo *stomp_info) {
	if (stomp_info->adapter.status == destroyed) return -1;

	if (stomp_destroy_internal(stomp_info, 1) != 0) return -1;

	return stomp_connect(stomp_info, &stomp_info->connect_headers, stomp_info->connect_callback, stomp_info->error_callback);
}

static int onopen_callback(StompAdapter *adapter) {
	StompAdapterStompInfo *custom_info = get_adapter_custom_data(adapter);
	StompInfo *stomp_info = custom_info->stomp_info;

	return stomp_send_connect(stomp_info);
}

static int onerror_callback_internal(StompAdapter *adapter, StompFrame *frame) {
	StompAdapterStompInfo *custom_info = get_adapter_custom_data(adapter);
	StompInfo *stomp_info = custom_info->stomp_info;

	adapter->status = disconnected;

	stomp_info->error_callback(stomp_info, frame);

	return 0;
}

static int onerror_callback(StompAdapter *adapter, char *message) {
	StompHeader header_array[1];
	header_array[0].name = "message";
	header_array[0].value = message;

	StompHeaders headers;
	headers.len = 1;
	headers.header_array = header_array;

	StompFrame frame;
	stomp_empty_frame(&frame);
	frame.command = "ERROR";
	frame.system_headers = &headers;

	return onerror_callback_internal(adapter, &frame);
}

static int onclose_callback(StompAdapter *adapter, char *message) {
	return onerror_callback(adapter, message);
}


static int onmessage_callback(StompAdapter *adapter, char *message) {
	StompAdapterStompInfo *custom_info = get_adapter_custom_data(adapter);
	StompInfo *stomp_info = custom_info->stomp_info;

	stomp_debug_print("stomp receive '%s'\n", message);

	StompFrame frame;
	stomp_empty_frame(&frame);

	if (stomp_frame_unmarshall(message, &frame)) return -1;

	char *command = frame.command;
	int ret;

	if (!strcmp(command, "CONNECTED")) {
		stomp_info->adapter.status = connected;

		stomp_info->connect_callback(stomp_info, &frame);

		ret = 0;
	} else if (!strcmp(command, "MESSAGE")) {
		StompHeader *header_subscription = stomp_find_header(frame.system_headers, "subscription");

		StompSubscription *subscription = stomp_find_subscription(stomp_info, header_subscription->value);
		if (subscription != NULL) {
			subscription->message_callback(stomp_info, &frame);
			ret = 0;
		} else {
			ret = -1;
		}
	} else if (!strcmp(command, "RECEIPT")) {
		ret = -1;
	} else if (!strcmp(command, "ERROR")) {
		onerror_callback_internal(adapter, &frame);
		ret = 0;
	} else {
		fprintf(stderr, "Invalid command %s\n", command);
		onerror_callback(adapter, "invalid stomp command");
		ret = -1;
	}

	stomp_unmarshall_frame_free(&frame);

	return ret;
}


static int onheartbeat_callback(StompAdapter *adapter) {
	return 0;
}


StompInfo stomp_create(StompAdapter *child_adapter) {
	StompInfo stomp_info;

	stomp_info.adapter.status = created;
	stomp_info.adapter.child_adapter = child_adapter;

	stomp_info.adapter.onopen_callback = onopen_callback;
	stomp_info.adapter.onmessage_callback = onmessage_callback;
	stomp_info.adapter.onerror_callback = onerror_callback;
	stomp_info.adapter.onheartbeat_callback = onheartbeat_callback;
	stomp_info.adapter.onclose_callback = onclose_callback;

	stomp_info.adapter.max_frame_length = child_adapter->max_frame_length;

	StompAdapterStompInfo *custom_data = malloc(sizeof(StompAdapterStompInfo));
	stomp_info.adapter.custom_data = custom_data;

	stomp_info.connect_headers.len = 0;
	stomp_info.subscriptions = NULL;
	stomp_info.last_server_action_time = time(NULL);
	stomp_info.next_subscription_id = 0;

	return stomp_info;
}

int stomp_init(StompInfo *stomp_info) {
	StompAdapterStompInfo *custom_data = get_adapter_custom_data(&stomp_info->adapter);
	custom_data->stomp_info = stomp_info;

	stomp_info->adapter.status = initialized;

	StompAdapter *child_adapter = stomp_info->adapter.child_adapter;
	return child_adapter->init_function(child_adapter, &stomp_info->adapter);
}
