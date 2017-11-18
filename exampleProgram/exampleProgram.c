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

volatile int test_stomp_force_exit = 0;
volatile int test_stomp_force_reconnect = 0;

void test_stomp_sighandler(int sig)
{
	test_stomp_force_exit = 1;
}

void test_stomp_message_callback(StompInfo *stomp_info, const StompFrame *frame) {
	fprintf(stdout, "command %s message %s\n", frame->command, frame->body);
}

void test_stomp_subscribe(StompInfo *stomp_info) {
	char *topic = (char *)stomp_info->custom_data;

	fprintf(stdout, "subscribing %s !\n", topic);

	char *suscription_id = stomp_subscribe(stomp_info, topic, test_stomp_message_callback, NULL);
}

void test_stomp_connect_callback(StompInfo *stomp_info, const StompFrame *frame) {
	fprintf(stdout, "connected %s !\n", frame->command);

	test_stomp_subscribe(stomp_info);
}

void test_stomp_error_callback(StompInfo *stomp_info, const StompFrame *frame) {
	fprintf(stderr, "error callback %s !\n", frame->command);

	StompHeader *header = stomp_find_header(frame->system_headers, "message");

	if (header != NULL && header->value != NULL) {
		fprintf(stderr, "error message %s\n", header->value);
	}
	if (header != NULL && header->value != NULL && strstr(header->value, "AccessDeniedException") != NULL) {
		test_stomp_force_exit = 1;
	} else {
		test_stomp_force_reconnect = 1;
	}

	return;
}


int main(int argc, char **argv) {

  if (argc < 3) goto usage;

  char *fullURL = argv[1];
  char *token = argv[2];
  char *topic = argv[3];

  signal(SIGINT, test_stomp_sighandler);

  fprintf(stdout, "connecting %s !\n", fullURL);

  StompAdapter ws_adapter = stomp_libwebsockets_adapter(fullURL);
  StompInfo stomp_info = stomp_create(&ws_adapter);

  stomp_init(&stomp_info);

  StompHeader header_array[1];
  header_array[0].name = "Authorization";
  header_array[0].value = token;

  StompHeaders headers;
  headers.len = 1;
  headers.header_array = header_array;

  if (stomp_connect(&stomp_info, &headers, test_stomp_connect_callback, test_stomp_error_callback)) {
	fprintf(stderr, "error initializing");
	return 1;
  }

  stomp_info.custom_data = topic;

  while (!test_stomp_force_exit) {
  	stomp_service(&stomp_info, 500);

  	if (test_stomp_force_reconnect) {
  		sleep(1);
  		stomp_reconnect(&stomp_info);
  		test_stomp_force_reconnect = 0;
  	}
  }

  stomp_destroy(&stomp_info);

  return 0;

  usage:
  	fprintf(stderr, "Usage: exampleProgram <wsAddress> <AuthorizationHeader> <topic> \n");
  	return 1;
}
