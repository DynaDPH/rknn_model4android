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

#include "json-c/json.h"
#include "uds_bus.h"

#define PATH_LEN 256
#define IMAGE_COUNT 20

bool debug = false;

#define DEBUG(fmt, ...)                                                     \
  if (debug) {                                                              \
    printf("[%s] [%d] : " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__); \
  }

struct option_s {
  char uds_path[PATH_LEN];
  char *file_list[IMAGE_COUNT];
  int file_count;
  int port;
  char ip_addr[PATH_LEN];
};

static void free_option(struct option_s *option) {
  for (int i = 0; i < option->file_count; i++) {
    free(option->file_list[i]);
  }
}
static int split(const char *str, char **list, int max_len, char sep) {
  if (!str || !list) return 0;
  int count = 0;
  char *tmp = strdup(str);
  char *token = strtok(tmp, &sep);

  while (token != NULL && count < max_len) {
    list[count++] = strdup(token);
    DEBUG("file[%d]=%s", count - 1, list[count - 1]);
    token = strtok(NULL, &sep);
  }
  free(tmp);
  return count;
}
static void print_usage(const char *program_name) {
  printf("Usage: %s [option]\n", program_name);
  printf("  --help               Print help and exit.\n");
  printf("  --debug              Print debug info.\n");
  printf("  --uds-path <string>  UDS path. If port is not set\n");
  printf("  --port <int>         TCP port number. If uds-path is not set\n");
  printf("  --ip-addr <string>   IP address.If port set\n");
  printf("  --file-list <string> File list.\n");
  printf(
      "example: %s --uds-path /tmp/sjtuai.sock[--port 9527 --ip-addr "
      "192.168.10.100] --file-list /path/to/image1.jpg,/path/to/image2.jpg\n",
      program_name);
}
static void option_get(int argc, char **argv, struct option_s *g_option) {
  int opt = 0;
  struct option long_options[] = {{"help", no_argument, NULL, 'h'},
                                  {"uds-path", required_argument, NULL, 'u'},
                                  {"port", required_argument, NULL, 'p'},
                                  {"debug", no_argument, NULL, 'd'},
                                  {"file-list", required_argument, NULL, 'f'},
                                  {"ip-addr", required_argument, NULL, 'i'},
                                  {NULL, 0, NULL, 0}};

  while ((opt = getopt_long(argc, argv, "hu:p:df:i:", long_options, NULL)) !=
         -1) {
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
      case 'f': {
        g_option->file_count =
            split(optarg, g_option->file_list, IMAGE_COUNT, ',');
      } break;
      case 'i': {
        strcpy(g_option->ip_addr, optarg);
      } break;
      default: {
        print_usage(argv[0]);
        exit(-1);
      } break;
    }
  }
}
static uint8_t *list_to_jsonarray_data(char **list, int count,
                                       uint32_t *data_size) {
  json_object *json_array = NULL;
  const char *json_str = NULL;
  uint8_t *result = NULL;
  size_t len = 0;
  int i = 0;
  uint8_t *ret = NULL;

  if (!list || count <= 0) {
    goto exit;
  }

  json_array = json_object_new_array();
  if (!json_array) {
    DEBUG("Failed to create JSON array object");
    goto exit;
  }
  for (i = 0; i < count; i++) {
    if (list[i] != NULL) {
      json_object *json_str_obj = json_object_new_string(list[i]);
      if (!json_str_obj) {
        DEBUG("Failed to create JSON string object for item %d", i);
        goto exit;
      }
      json_object_array_add(json_array, json_str_obj);
    }
  }

  json_str = json_object_to_json_string(json_array);
  if (!json_str) {
    DEBUG("Failed to convert JSON object to string");
    goto exit;
  }

  len = strlen(json_str);
  result = (uint8_t *)malloc(len + 1);
  if (!result) {
    DEBUG("Failed to allocate memory for result");
    goto exit;
  }

  memcpy(result, json_str, len + 1);
  ret = result;
  if (data_size) {
    *data_size = len + 1;
  }

exit:
  if (json_array) {
    json_object_put(json_array);
  }

  if (ret == NULL) {
    if (result) {
      free(result);
    }
  }
  return ret;
}
int main(int argc, char **argv) {
  struct uds_server_s server = {0};
  struct option_s g_option = {0};
  struct bus_message_s *msg = NULL;
  int sockfd = -1;
  uint8_t *buffer = NULL;
  uint32_t data_size = 0;

  option_get(argc, argv, &g_option);
  if (g_option.file_count == 0 ||
      (g_option.port == 0 && g_option.uds_path[0] == '\0')) {
    DEBUG("Please set uds-path or port, file-list is required");
    print_usage(argv[0]);
    exit(0);
  }
  DEBUG("option uds_path: %s, port: %d, file_count: %d", g_option.uds_path,
        g_option.port, g_option.file_count);

  if (strlen(g_option.uds_path) > 0) {
    sockfd = uds_client_create(g_option.uds_path);
  } else {
    sockfd = tcp_client_create(g_option.ip_addr, g_option.port);
  }

  sleep(1);
  buffer = list_to_jsonarray_data(g_option.file_list, g_option.file_count,
                                  &data_size);
  msg = bus_message_new_external_request(UDS_CALL_TYPE_CLIP, buffer, data_size);
  if (msg != NULL) {
    if (send_bus_message(sockfd, msg) < 0) {
      DEBUG("send bus message failed");
    }
  }

  recv_bus_message(sockfd, 3000);

end:
  if (sockfd != -1) {
    close(sockfd);
  }
  uds_close(&server);
  return 0;
}