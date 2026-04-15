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
#include <sys/un.h>
#include <sys/wait.h>
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

struct option_s g_option = {0};

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
static int split_cmd_args(const char *cmd_str, char **args, int max_args) {
  int arg_count = 0;
  int ret = -1;
  const char *p = NULL;
  const char *start = NULL;
  char quote_char = 0;
  int in_quote = 0;
  char *arg_buf = NULL;
  int arg_len = 0;

  if (cmd_str == NULL || args == NULL || max_args < 2) {
    LOG("Invalid parameters for split_cmd_args");
    goto end;
  }

  if (strlen(cmd_str) == 0) {
    LOG("Empty command string");
    goto end;
  }

  p = cmd_str;
  while (*p != '\0' && arg_count < max_args - 1) {
    // Skip leading whitespace
    while (*p == ' ' || *p == '\t') {
      p++;
    }
    if (*p == '\0') break;

    start = p;
    in_quote = 0;
    quote_char = 0;

    // Check if argument starts with a quote
    if (*p == '\'' || *p == '"') {
      quote_char = *p;
      in_quote = 1;
      p++;
      start = p;  // Start after the opening quote

      // Find closing quote
      while (*p != '\0' && *p != quote_char) {
        p++;
      }
      arg_len = p - start;
      if (*p == quote_char) {
        p++;  // Skip closing quote
      }
    } else {
      // Find end of unquoted argument (handle embedded quotes)
      while (*p != '\0') {
        if (*p == '\'' || *p == '"') {
          // Embedded quote - scan to matching quote
          char q = *p;
          p++;
          while (*p != '\0' && *p != q) {
            p++;
          }
          if (*p == q) p++;
        } else if (*p == ' ' || *p == '\t') {
          break;
        } else {
          p++;
        }
      }
      arg_len = p - start;
    }

    if (arg_len > 0) {
      arg_buf = (char *)malloc(arg_len + 1);
      if (arg_buf == NULL) {
        LOG("Failed to allocate memory for argument");
        goto end;
      }
      memcpy(arg_buf, start, arg_len);
      arg_buf[arg_len] = '\0';
      args[arg_count++] = arg_buf;
    }
  }
  args[arg_count] = NULL;

  if (arg_count == 0) {
    LOG("No valid arguments found in command string");
    goto end;
  }

  ret = arg_count;

end:
  return ret;
}

static pid_t start_process(const char *cmd_str, int block, int *exit_code) {
  char *args[64] = {NULL};
  int arg_count = 0;
  pid_t pid = -1;
  int status = 0;
  pid_t ret = -1;
  int code = -1;
  int sig = 0;

  if (cmd_str == NULL || strlen(cmd_str) == 0) {
    LOG("Command string is empty");
    goto end;
  }
  if (exit_code != NULL) {
    *exit_code = -1;
  }

  arg_count = split_cmd_args(cmd_str, args, 64);
  if (arg_count == -1) {
    LOG("split_cmd_args failed");
    goto end;
  }

  pid = fork();
  if (pid == -1) {
    LOG("fork failed");
    goto end;
  }

  if (pid == 0) {
    execvp(args[0], args);
    LOG("execvp failed");
    exit(EXIT_FAILURE);
  }

  LOG("Child process started, PID: %d, Command: %s", pid, cmd_str);

  if (block) {
    ret = waitpid(pid, &status, 0);
    if (ret == -1) {
      LOG("waitpid failed");
      goto end;
    }
    if (WIFEXITED(status)) {
      code = WEXITSTATUS(status);
      if (exit_code != NULL) {
        *exit_code = code;
      }
      LOG("Child process %d exited normally, exit code: %d", pid, code);
    } else if (WIFSIGNALED(status)) {
      sig = WTERMSIG(status);
      LOG("Child process %d killed by signal: %d", pid, sig);
      if (exit_code != NULL) {
        *exit_code = -sig;
      }
    }
  }

  ret = pid;

end:
  return ret;
}

static int reap_process(pid_t pid) {
  int status = 0;
  pid_t ret = -1;
  int code = -1;
  int sig = 0;

  if (pid <= 0) {
    LOG("Invalid PID: %d", pid);
    goto end;
  }

  ret = waitpid(pid, &status, WNOHANG);
  if (ret == -1) {
    LOG("waitpid failed");
    goto end;
  } else if (ret == 0) {
    ret = -1;  // Process still running
    goto end;
  }

  if (WIFEXITED(status)) {
    ret = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    ret = -WTERMSIG(status);
  }

end:
  return ret;
}

/**
 *
 * static void help_guide(char *prog_name) {
  printf("Usage: %s [OPTIONS] [ARGS]\n", prog_name);
  printf("Options:\n");
  printf("--image-model-path,-i load image model path\n");
  printf("--text-model-path,-t load text model path\n");
  printf("--labels,-l load text path\n");
  printf("--unix-domain-socket,-u connect UDS\n");
  printf("--data load data\n");
  printf("--data-size load data size\n");
  printf("--help, -h  help guide\n");
}
 */

#define CLIP_PROCESS ROOT_DIR"/clipmaster"
#define CLIP_IMAGE_MODEL_PATH ROOT_DIR"/model/clip_images.rknn"
#define CLIP_TEXT_MODEL_PATH ROOT_DIR"/model/clip_text.rknn"
#define CLIP_LABELS_PATH ROOT_DIR"/model/clip-lables.json"
#define YOLO_LABELS_PATH ROOT_DIR"/model/coco80-labels.csv"
#define YOLO_MODEL_PATH ROOT_DIR"/model/yolov8.rknn"
void *clip_call(int src_fd, uint32_t data_size, uint8_t *msg_data) {
  char *cmd = NULL;
  int cmd_len = 0;
  pid_t pid = -1;
  int exit_code = 0;
  void *ret = NULL;

  if (msg_data == NULL || data_size == 0) {
    LOG("Invalid parameters for clip_call");
    goto end;
  }

  // 计算命令长度
  cmd_len = strlen(CLIP_PROCESS) + 
            strlen(" --clip-image-model-path '") + strlen(CLIP_IMAGE_MODEL_PATH) +
            strlen("' --clip-text-model-path '") + strlen(CLIP_TEXT_MODEL_PATH) +
            strlen("' --clip-labels-path '") + strlen(CLIP_LABELS_PATH) +
            strlen("' --yolo8-model-path '") + strlen(YOLO_MODEL_PATH) +
            strlen("' --yolo8-labels-path '") + strlen(YOLO_LABELS_PATH) +
            strlen("' --unix-domain-socket '") + strlen(g_option.uds_path) +
            strlen("' --data '") + data_size +
            strlen("'") + 64;

  cmd = (char *)malloc((size_t)cmd_len);
  if (cmd == NULL) {
    LOG("Failed to allocate memory for command");
    goto end;
  }

  // 构建命令字符串
  snprintf(cmd, (size_t)cmd_len,
           "%s --clip-image-model-path '%s' "
           "--clip-text-model-path '%s' "
           "--clip-labels-path '%s' "
           "--yolo8-model-path '%s' "
           "--yolo8-labels-path '%s' "
           "--unix-domain-socket '%s' "
           "--data '%s'",
           CLIP_PROCESS,
           CLIP_IMAGE_MODEL_PATH,
           CLIP_TEXT_MODEL_PATH,
           CLIP_LABELS_PATH,
           YOLO_MODEL_PATH,
           YOLO_LABELS_PATH,
           g_option.uds_path,
           (char *)msg_data);

  LOG("Executing CLIP command: %s", cmd);
  pid = start_process(cmd, 0, &exit_code);

  if (pid <= 0) {
    LOG("Failed to start CLIP process, command: %s", cmd);
    goto end;
  }

  LOG("CLIP process started successfully with PID: %d", pid);

  ret = (void *)(intptr_t)pid;

end:
  if (cmd != NULL) {
    free(cmd);
  }
  return ret;
}
int main(int argc, char **argv) {
  struct uds_server_s server = {0};

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

  if (uds_register_handler(UDS_CALL_TYPE_CLIP, clip_call) < 0) {
    DEBUG("uds_register_handler failed");
    goto end;
  }
  uds_event_loop(&server);
end:
  uds_close(&server);
  return 0;
}