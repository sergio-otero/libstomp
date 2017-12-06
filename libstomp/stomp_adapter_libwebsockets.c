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
#include <math.h>

#include <libwebsockets.h>

#include "libstomp.h"

typedef struct {
	struct lws *wsi; // Websocket instance
	struct lws_context *context;
	struct lws_protocols *protocols;
	char *url;
} StompAdapterLibWebSocketsData;

static StompAdapterLibWebSocketsData* get_adapter_custom_data(StompAdapter *adapter) {
	return (StompAdapterLibWebSocketsData*)adapter->custom_data;
}

int stomp_libwebsockets_callback_lws_http(struct lws *wsi, enum lws_callback_reasons reason,
			void *user, void *in, size_t len);

int stomp_libwebsockets_callback_lws_websocket(struct lws *wsi, enum lws_callback_reasons reason,
			void *user, void *in, size_t len);

/* list of supported protocols and callbacks */
enum stomp_lws_protocol_enum {
		PROTOCOL_HTTP,
		PROTOCOL_STOMP12
	};

static const struct lws_extension stomp_lws_exts[] = {
	{
		"permessage-deflate",
		lws_extension_callback_pm_deflate,
		"permessage-deflate; client_no_context_takeover"
	},
	{
		"deflate-frame",
		lws_extension_callback_pm_deflate,
		"deflate_frame"
	},
	{ NULL, NULL, NULL /* terminator */ }
};

static int init_function(StompAdapter *adapter, StompAdapter *parent_adapter) {
	if (adapter->status != created) return -1;

	adapter->parent_adapter = parent_adapter;

	adapter->status = initialized;

	return 0;
}

static int connect_function (StompAdapter *adapter) {
	if (adapter->status != initialized) return -1;

	int use_ssl = 0, ret = 0, ietf_version = -1;
	unsigned int pp_secs = 0;
	struct lws_context_creation_info info;
	struct lws_client_connect_info i;

	const char *prot, *p, *url;
	char fullURL[300];
	char path[300];
	char token[300];
	char deviceId[300];

	StompAdapterLibWebSocketsData *custom_data = get_adapter_custom_data(adapter);
	url = custom_data->url;

	memset(&info, 0, sizeof info);

	memset(&i, 0, sizeof(i));

	// Copiamos url a fullURL pq la funcion toca el string
	strncpy(fullURL, url, sizeof(fullURL) - 1);
	if (lws_parse_uri(fullURL, &prot, &i.address, &i.port, &p)) {
		fprintf(stderr, "Error parsing URL %s\n", url);
		return -1;
	}

	/* add back the leading / on path */
	path[0] = '/';
	strncpy(path + 1, p, sizeof(path) - 2);
	path[sizeof(path) - 1] = '\0';
	i.path = path;

	if (!strcmp(prot, "http") || !strcmp(prot, "ws")) {
		use_ssl = 0;
	}
	if (!strcmp(prot, "https") || !strcmp(prot, "wss")) {
		if (!use_ssl) {
			use_ssl = LCCSCF_USE_SSL;
		}
	}

	/*
	* create the websockets context.  This tracks open connections and
	* knows how to route any traffic and which protocol version to use,
	* and if each connection is client or server side.
	*
	* We tell it to not listen on any port.
	*/
	
	// cannot be global const because max_frame_length
	// destination is const. This is the only way i've found to declare it and mantain after function ends
	const struct lws_protocols stomp_lws_protocols[] = {
	    // first protocol must always be HTTP handler
	    {
	        "http-only",                // name
			stomp_libwebsockets_callback_lws_http,          // callback
	        0,                          // per_session_data_size
			adapter->max_frame_length // rx_buffer_size
	    },
	    {
	        "v12.stomp",                // protocol name - very important!
			stomp_libwebsockets_callback_lws_websocket,      // callback
	        0,                           // we don't use any per session data
			adapter->max_frame_length // rx_buffer_size
	    },
	    {
	        NULL, NULL, 0               // End of list
	    }
	};
	
	int size = 3*sizeof(struct lws_protocols);
	custom_data->protocols = malloc(size);
	memcpy(custom_data->protocols, stomp_lws_protocols, size);

	info.protocols = custom_data->protocols;

	info.port = CONTEXT_PORT_NO_LISTEN;
	info.gid = -1;
	info.uid = -1;
	info.ws_ping_pong_interval = pp_secs;
	info.extensions = stomp_lws_exts;
	info.max_http_header_data = 2048;

#if defined(LWS_OPENSSL_SUPPORT)
	info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
#endif

	custom_data->context = lws_create_context(&info);
	if (custom_data->context == NULL) {
		free(custom_data->protocols);
		fprintf(stderr, "Creating libwebsocket context failed\n");
		return -1;
	}

	i.context = custom_data->context;
	i.ssl_connection = use_ssl;
	i.host = i.address;
	i.origin = i.address;
	i.ietf_version_or_minus_one = ietf_version;

	stomp_debug_print("using %s mode (ws)\n", prot);

	/*
	 * nothing happens until the client websocket connection is
	 * asynchronously established... calling lws_client_connect() only
	 * instantiates the connection logically, lws_service() progresses it
	 * asynchronously.
	 */

	stomp_debug_print("Opening socket \n");
	i.protocol = info.protocols[PROTOCOL_STOMP12].name;
	i.pwsi = &custom_data->wsi;

	i.userdata = adapter;

	struct lws *result = lws_client_connect_via_info(&i);

	if (!result) {
		free(custom_data->protocols);
		fprintf(stderr, "Error opening socket!\n");
		return -1;
	}

	adapter->status = preconnected;

	return 0;
}

static int send_function (StompAdapter *adapter, char *message) {
	if (adapter->status != connected) return -1;

	StompAdapterLibWebSocketsData *custom_data = get_adapter_custom_data(adapter);

	int max_frame_length = adapter->max_frame_length;

	//TODO reuse buffer
	char buffer[LWS_PRE + max_frame_length];

	int message_len = strlen(message) + 1; // send the null char

	if (strlen(message) > max_frame_length) {
		fprintf(stderr, "message exceed STOMP_MAX_FRAME_BUFFER %d > %d", message_len, max_frame_length);
		return -1;
	}

	strcpy(&buffer[LWS_PRE], message);

	int n = lws_write(custom_data->wsi, (unsigned char *)&buffer[LWS_PRE], message_len, LWS_WRITE_TEXT);
	if (n < 0)
		return -1;

	/* we only had one thing to send, so inform lws we are done
	 * if we had more to send, call lws_callback_on_writable(wsi);
	 * and just return 0 from callback.  On having sent the last
	 * part, call the below api instead.*/
	lws_client_http_body_pending(custom_data->wsi, 0);

	return 0;

}

static int service_function (StompAdapter *adapter, int timeout_ms) {
	if (adapter->status != preconnected && adapter->status != connected) return -1;

	StompAdapterLibWebSocketsData *custom_data = get_adapter_custom_data(adapter);

	int status = lws_service(custom_data->context, timeout_ms);
	if (status != 0) {
		fprintf(stderr, "Status is %d!!!!!!\n", status);
	}
	return status;
}

static int destroy_function_internal (StompAdapter *adapter, int reconnect) {
	if (adapter->status == destroyed) return -1;

	StompAdapterLibWebSocketsData *custom_data = get_adapter_custom_data(adapter);

	if (custom_data->context) {
		lws_context_destroy(custom_data->context);
	}
	if (custom_data->protocols) {
		free(custom_data->protocols);
	}

	if (reconnect) {
		adapter->status = initialized;
	} else {
		free(adapter->custom_data);
		adapter->status = destroyed;
	}

	return 0;
}

static int destroy_function (StompAdapter *adapter) {
	return destroy_function_internal(adapter, 0);
}

static int restart_function(StompAdapter *adapter) {
	if (adapter->status == destroyed) return -1;

	if (destroy_function_internal(adapter, 1) != 0) return -1;

	adapter->status = initialized;

	return 0;
}

StompAdapter stomp_libwebsockets_adapter(char *url, int max_frame_length) {
	StompAdapter adapter;

	adapter.status = created;
	adapter.init_function = init_function;
	adapter.service_function = service_function;
	adapter.connect_function = connect_function;
	adapter.send_function = send_function;
	adapter.restart_function = restart_function;
	adapter.destroy_function = destroy_function;
	adapter.max_frame_length = max_frame_length;

	StompAdapterLibWebSocketsData *custom_data = malloc(sizeof(StompAdapterLibWebSocketsData));
	custom_data->url = url;
	custom_data->context = NULL;
	custom_data->wsi = NULL;
	custom_data->protocols = NULL;
	adapter.custom_data = custom_data;

	return adapter;
}

int stomp_libwebsockets_callback_lws_http(struct lws *wsi, enum lws_callback_reasons reason,
			void *user, void *in, size_t len) {
	StompAdapter *adapter = (StompAdapter *)user;
	StompAdapter *parent_adapter = adapter != NULL ? adapter->parent_adapter : NULL;
	char *message = (char *)in;

	switch (reason) {
		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
			if (adapter->status != preconnected && adapter->status != connected) return 0;

			parent_adapter->onclose_callback(parent_adapter, message);
			break;
	}

	return 0;
}

int stomp_libwebsockets_callback_lws_websocket(struct lws *wsi, enum lws_callback_reasons reason,
			void *user, void *in, size_t len)
{
	stomp_debug_print("stomp_callback http %i !!\n", reason);

	StompAdapter *adapter = (StompAdapter *)lws_wsi_user(wsi);
	StompAdapter *parent_adapter = NULL;

	if (adapter != NULL) parent_adapter = adapter->parent_adapter;

	char *message = (char *)in;

	switch (reason) {
		case LWS_CALLBACK_WS_EXT_DEFAULTS:
			// Change the deflate buffer to fit messages in 1 callback
			if (!strcmp(user, "permessage-deflate")) {
				int max_frame_size = adapter->max_frame_length;

				// 2^rx_buf_size = max_frame_size  => rx_buf_size = log2(max_frame_size)
				int rx_buf_size = ceil(log(max_frame_size) / log(2));

				sprintf(in, "rx_buf_size=%i", rx_buf_size); // 2^n=2048)
			}
			break;
		case LWS_CALLBACK_CLIENT_ESTABLISHED:
			if (adapter->status != connected && adapter->status != preconnected) return 0;

			adapter->status = connected;

			parent_adapter->onopen_callback(parent_adapter);

			break;
		case LWS_CALLBACK_CLIENT_RECEIVE:
			if (adapter->status != connected && adapter->status != preconnected) return 0;

			if (len == 0) return 0;

			message[len] = '\0';

			parent_adapter->onmessage_callback(parent_adapter, message);

			break;
		case LWS_CALLBACK_CLOSED:
			if (adapter->status != connected && adapter->status != preconnected) return 0;

			adapter->status = disconnected;

			parent_adapter->onclose_callback(parent_adapter, message);

			break;
		default:
			break;
	}

	return 0;
}
