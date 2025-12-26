// Copyright (c) 2024 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uds_bus.h"

#define PATH_LEN 256

bool debug = false;

#define DEBUG(fmt, ...)                                                     \
  if (debug) {                                                              \
    printf("[%s] [%d] : " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__); \
  }

struct option_s {
  char uds_path[PATH_LEN];
  int port;
};
static void print_usage(const char *program_name) {
  printf("Usage: %s [option]\n", program_name);
  printf("  --help               Print help and exit.\n");
  printf("  --debug              Print debug info.\n");
  printf("  --uds-path <string>  UDS path.\n");
  printf("  --port <int>         TCP port number.\n");
}

static void option_get(int argc, char **argv, struct option_s *g_option) {
  int opt = 0;
  struct option long_options[] = {{"help", no_argument, NULL, 'h'},
                                  {"uds-path", required_argument, NULL, 'u'},
                                  {"port", required_argument, NULL, 'p'},
                                  {"debug", no_argument, NULL, 'd'},
                                  {NULL, 0, NULL, 0}};

  while ((opt = getopt_long(argc, argv, "hud:p:", long_options, NULL)) != -1) {
    switch (opt) {
      case 'h': {
        print_usage(argv[0]);
        exit(0);
      }
      case 'u': {
        strcpy(g_option->uds_path, optarg);
      } break;
      case 'p': {
        g_option->port = atoi(optarg);
      } break;
      case 'd': {
        debug = true;
      } break;
      default: {
        print_usage(argv[0]);
        exit(-1);
      } break;
    }
  }
}

int main(int argc, char **argv) {
  struct uds_server_s server = {0};
  struct option_s g_option = {0};

  option_get(argc, argv, &g_option);
  if (g_option.uds_path[0] == '\0') {
    DEBUG("uds_path is empty, use default path %s", UDS_PATH);
    strcpy(g_option.uds_path, UDS_PATH);
  }

  if (g_option.port == 0) {
    DEBUG("port is empty, use default port %d", TCP_PORT);
    g_option.port = TCP_PORT;
  }

  DEBUG("Listen to uds_path %s, port %d", g_option.uds_path, g_option.port);

  if (uds_server_create(&server, g_option.uds_path, g_option.port) < 0) {
    DEBUG("uds_server_create failed");
  }
  uds_event_loop(&server);
end:
  uds_close(&server);
  return 0;
}