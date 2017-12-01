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
#include <signal.h>
#include <unistd.h>

#include "libstomp.h"
#include "minunit.h"

static StompAdapter test_adapter;
static StompInfo stomp_info;

static int expected_init;
static int expected_connect;
static int expected_send;
static char expected_send_message[2048];
static int expected_destroy;
static int expected_restart;
static int expected_service;
static int expected_connect_callback;
static int expected_error_callback;
static char expected_frame_msg[2048];
static int expected_message_callback;


static int expected_adapter_call_error;
static char expected_adapter_call_error_message[256];

#define stomp_adapter_assert() mu_assert(expected_adapter_call_error == 0, expected_adapter_call_error_message);
#define MU_SUB_TEST(test) MU__SAFE_BLOCK(\
	test();\
	if (minunit_status) {\
		return;\
	}\
)


static void check_adapter_function(int *valuePointer, int expected, char *message, char *error_message) {
	int value = *valuePointer;

	if (value != expected && expected_adapter_call_error == 0) {
		expected_adapter_call_error = 1;
		strcpy(expected_adapter_call_error_message, error_message);
	}
}

static void check_adapter_frame(const StompFrame *frame) {
	char frameStr[2048];

	if (expected_adapter_call_error == 0) {
		stomp_frame_marshall(frame, frameStr, 2048);

		if (strcmp(expected_frame_msg, frameStr)) {
			expected_adapter_call_error = 1;
			strcpy(expected_adapter_call_error_message, frameStr);
		}
	}
}

static int init_function(StompAdapter *adapter, StompAdapter *parent_adapter) {
	check_adapter_function(&expected_init, 1, NULL, "init not expected");
	expected_init = 0;

	adapter->parent_adapter = parent_adapter;

	return 0;
}

static int connect_function (StompAdapter *adapter) {
	check_adapter_function(&expected_connect, 1, NULL, "connect not expected");

	return 0;
}

static int send_function (StompAdapter *adapter, char *message) {
	check_adapter_function(&expected_send, 1, message, "send not expected");
	if (strcmp(expected_send_message, message)) {
		expected_adapter_call_error = 1;
		strcpy(expected_adapter_call_error_message, message);
	}

	return 0;
}


static int destroy_function (StompAdapter *adapter) {
	check_adapter_function(&expected_destroy, 1, NULL, "destroy not expected");

	return 0;
}

static int restart_function(StompAdapter *adapter) {
	check_adapter_function(&expected_restart, 1, NULL, "restart not expected");

	return 0;
}

static int service_function (StompAdapter *adapter, int timeout_ms) {
	check_adapter_function(&expected_service, 1, NULL, "service not expected");

	return 0;
}

StompAdapter create_test_adapter() {
	StompAdapter adapter;

	adapter.status = created;
	adapter.init_function = init_function;
	adapter.service_function = service_function;
	adapter.connect_function = connect_function;
	adapter.send_function = send_function;
	adapter.restart_function = restart_function;
	adapter.destroy_function = destroy_function;

	return adapter;
}

void test_setup(void) {
	expected_init = 0;
	expected_connect = 0;
	expected_send = 0;
	strcpy(expected_send_message, "");
	expected_destroy = 0;
	expected_restart = 0;
	expected_service = 0;
	expected_adapter_call_error = 0;
	expected_connect_callback = 0;
	expected_error_callback = 0;
	strcpy(expected_frame_msg, "");
	expected_message_callback = 0;

	test_adapter = create_test_adapter();
	stomp_info = stomp_create(&test_adapter);

	expected_init = 1;
	stomp_init(&stomp_info);
}

void test_teardown(void) {
	expected_destroy = 1;
	stomp_destroy(&stomp_info);
}

void test_stomp_connect_callback(StompInfo *stomp_info, const StompFrame *frame) {
	check_adapter_function(&expected_connect_callback, 1, NULL, "connect callback not expected");
	check_adapter_frame(frame);
}

void test_stomp_error_callback(StompInfo *stomp_info, const StompFrame *frame) {
	check_adapter_function(&expected_error_callback, 1, NULL, "error callback not expected");
	check_adapter_frame(frame);
}

void test_stomp_message_callback(StompInfo *stomp_info, const StompFrame *frame) {
	check_adapter_function(&expected_message_callback, 1, NULL, "error subcribe not expected");
	check_adapter_frame(frame);
}

MU_TEST(test_init) {
	stomp_adapter_assert();

	mu_assert_int_eq(0, expected_init);

	mu_assert_int_eq(0, stomp_info.next_subscription_id);
	mu_assert_int_eq(0, stomp_info.connect_headers.len);
	mu_assert(stomp_info.subscriptions == NULL, "null subscriptions");

	mu_assert(stomp_info.adapter.child_adapter == &test_adapter, "child_adapter");
	mu_assert(stomp_info.adapter.status == initialized, "status initialized");

	mu_assert(test_adapter.parent_adapter == &stomp_info.adapter, "test_adapter parent");
}

void preconnect() {
	stomp_adapter_assert();

	StompHeader header_array[1];
	header_array[0].name = "Authorization";
	header_array[0].value = "token";

	StompHeaders headers;
	headers.len = 1;
	headers.header_array = header_array;

	expected_connect = 1;
	int res = stomp_connect(&stomp_info, &headers, test_stomp_connect_callback, test_stomp_error_callback);
	mu_assert_int_eq(0, res);
	stomp_adapter_assert();

	expected_service = 1;
	res = stomp_service(&stomp_info, 0);
	mu_assert_int_eq(0, res);
	stomp_adapter_assert();
}

void connect() {
	MU_SUB_TEST(preconnect);

	expected_send = 1;
	strcpy(expected_send_message, "CONNECT\naccept-version:1.1,1.0\nheart-beat:10000,10000\nAuthorization:token\n\n");

	test_adapter.parent_adapter->onopen_callback(test_adapter.parent_adapter);
	stomp_adapter_assert();

	expected_connect_callback = 1;
	char str_connected[] = "CONNECTED\n";

	strcpy(expected_frame_msg, "CONNECTED\n\n");

	test_adapter.parent_adapter->onmessage_callback(test_adapter.parent_adapter, str_connected);

	stomp_adapter_assert();
	mu_assert(stomp_info.adapter.status == connected, "status connected");
}

MU_TEST(test_connect_ok) {
	MU_SUB_TEST(connect);
}


MU_TEST(test_connect_ko_connection) {
	MU_SUB_TEST(preconnect);

	expected_error_callback = 1;
	strcpy(expected_frame_msg, "ERROR\nmessage:test no connection\n\n\0");

	test_adapter.parent_adapter->onclose_callback(test_adapter.parent_adapter, "test no connection");
	stomp_adapter_assert();

	mu_assert(stomp_info.adapter.status == disconnected, "status closed");
}

MU_TEST(test_connect_ko_unauthorized) {
	MU_SUB_TEST(preconnect);

	expected_send = 1;
	strcpy(expected_send_message, "CONNECT\naccept-version:1.1,1.0\nheart-beat:10000,10000\nAuthorization:token\n\n");

	test_adapter.parent_adapter->onopen_callback(test_adapter.parent_adapter);
	stomp_adapter_assert();

	expected_error_callback = 1;
	strcpy(expected_frame_msg, "ERROR\nmessage:AccessDeniedException\n\n\0");

	char str_connected[] = "ERROR\nmessage:AccessDeniedException\ncontent-length:0\n";
	test_adapter.parent_adapter->onmessage_callback(test_adapter.parent_adapter, str_connected);

	stomp_adapter_assert();

	mu_assert(stomp_info.adapter.status == disconnected, "status disconnected");
}

MU_TEST(test_subcribe_ok) {
	MU_SUB_TEST(connect);

	expected_send = 1;
	strcpy(expected_send_message, "SUBSCRIBE\ndestination:/queue\nid:sub-0\n\n");

	stomp_subscribe(&stomp_info, "/queue", test_stomp_message_callback, NULL);
	stomp_adapter_assert();

	// Mensaje otro subscriptor no se recibe
	char str_connected[] = "MESSAGE\nsubscription:sub-XX\nmessage-id:001\ncontent-type:json\n\nel mensaje\n\0";
	test_adapter.parent_adapter->onmessage_callback(test_adapter.parent_adapter, str_connected);
	stomp_adapter_assert();

	// Mensaje bueno si
	expected_message_callback = 1;
	strcpy(expected_frame_msg, "MESSAGE\nsubscription:sub-0\nmessage-id:001\ncontent-type:json\n\nel mensaje\n\0");

	strcpy(str_connected, "MESSAGE\nsubscription:sub-0\nmessage-id:001\ncontent-type:json\n\nel mensaje\n\0");
	test_adapter.parent_adapter->onmessage_callback(test_adapter.parent_adapter, str_connected);

	stomp_adapter_assert();

	mu_assert(stomp_info.adapter.status == connected, "status connected");

}

MU_TEST_SUITE(test_suite) {
	MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

	MU_RUN_TEST(test_init);
	MU_RUN_TEST(test_connect_ok);
	MU_RUN_TEST(test_connect_ko_connection);
	MU_RUN_TEST(test_connect_ko_unauthorized);
	MU_RUN_TEST(test_subcribe_ok);
}

int main(int argc, char *argv[]) {
	MU_RUN_SUITE(test_suite);
	MU_REPORT();
	return 0;
}
