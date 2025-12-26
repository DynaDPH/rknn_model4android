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

#include "clip.h"
#include "file_utils.h"
#include "image_utils.h"
#include "json-c/json.h"
#include "uds_bus.h"

#define PATH_LEN 256
struct option_s {
  char image_model_path[PATH_LEN + 1];
  char text_model_path[PATH_LEN + 1];
  char text_path[PATH_LEN + 1];
  char uds_path[PATH_LEN + 1];
};

struct clip_req_s {
  int image_count;
  char *image_path[MAX_IMAGE_COUNT];
};

struct images {
  char image[PATH_LEN];
  char text[PATH_LEN];
  float score;
};

struct clip_resp_s {
  int image_count;
  struct images *images[MAX_IMAGE_COUNT];
};

void free_clip_req(struct clip_req_s *msg) {
  for (int i = 0; i < msg->image_count; i++) {
    free(msg->image_path[i]);
  }
  free(msg);
}

void dump_clip_req(struct clip_req_s *msg) {
  printf("image_count: %d\n", msg->image_count);
  for (int i = 0; i < msg->image_count; i++) {
    printf("image_path[%d]: %s\n", i, msg->image_path[i]);
  }
}

void add_clip_resp(struct clip_resp_s *resp, const char *image_path,
                   const char *text, float score) {
  struct images *img = NULL;

  img = (struct images *)malloc(sizeof(struct images));
  if (img != NULL) {
    strncpy(img->image, image_path, PATH_LEN);
    strncpy(img->text, text, PATH_LEN);
    img->score = score;
    if (resp->image_count < MAX_IMAGE_COUNT) {
      resp->images[resp->image_count++] = img;
    } else {
      free(img);
      LOG("clip_resp: image_count > MAX_IMAGE_COUNT");
    }
  }
}

void free_clip_resp(struct clip_resp_s *resp) {
  int i;
  for (i = 0; i < resp->image_count; i++) {
    free(resp->images[i]);
  }
  free(resp);
}

static void help_guide(char *prog_name) {
  printf("Usage: %s [OPTIONS] [ARGS]\n", prog_name);
  printf("Options:\n");
  printf("--image-model-path,-i load image model path\n");
  printf("--text-model-path,-t load text model path\n");
  printf("--labels,-l load text path\n");
  printf("--unix-domain-socket,-u connect UDS\n");
  printf("--help, -h  help guide\n");
}

static int parser_option(struct option_s *uopt, int argc, char **argv) {
  int opt = 0;

  struct option longopt[] = {{"image-model-path", required_argument, NULL, 'i'},
                             {"text-model-path", required_argument, NULL, 't'},
                             {"help", no_argument, NULL, 'h'},
                             {NULL, 0, NULL, 0}};

  while ((opt = getopt_long(argc, argv, "i:t:h", longopt, NULL)) != -1) {
    switch (opt) {
      case 'i': {
        strncpy(uopt->image_model_path, optarg, sizeof(uopt->image_model_path));
      } break;
      case 't': {
        strncpy(uopt->text_model_path, optarg, sizeof(uopt->text_model_path));
      } break;
      case 'l': {
        strncpy(uopt->text_path, optarg, sizeof(uopt->text_path));
      } break;
      case 'u': {
        strncpy(uopt->uds_path, optarg, sizeof(uopt->uds_path));
      } break;
      case 'h': {
        help_guide(argv[0]);
        exit(0);
      } break;
    }
  }
  return 0;
}

static struct clip_req_s *parser_mgs_data(uint8_t *data) {
  json_object *json_obj = NULL;
  struct clip_req_s *msg = NULL;
  struct clip_req_s *ret = NULL;
  int array_len = 0;

  if (data == NULL) {
    LOG("data is null");
    goto end;
  }

  json_obj = json_tokener_parse((char *)data);
  if (json_obj == NULL) {
    LOG("Failed to parse JSON data");
    goto end;
  }

  msg = (struct clip_req_s *)malloc(sizeof(struct clip_req_s));
  if (msg == NULL) {
    goto end;
  }

  memset(msg, 0, sizeof(struct clip_req_s));

  if (!json_object_is_type(json_obj, json_type_array)) {
    LOG("JSON data is not an array");
    goto end;
  }

  array_len = json_object_array_length(json_obj);
  if (array_len > MAX_IMAGE_COUNT) {
    LOG("Array length %d exceeds maximum allowed %d", array_len,
        MAX_IMAGE_COUNT);
    goto end;
  }

  for (int i = 0; i < array_len; i++) {
    json_object *item = json_object_array_get_idx(json_obj, i);
    if (item != NULL && json_object_is_type(item, json_type_string)) {
      const char *str_val = json_object_get_string(item);
      if (str_val != NULL) {
        msg->image_path[i] = (char *)malloc(strlen(str_val) + 1);
        if (msg->image_path[i] != NULL) {
          strcpy(msg->image_path[i], str_val);
          msg->image_count++;
        }
      }
    }
  }
  dump_clip_req(msg);
  ret = msg;
end:
  if (ret == NULL) {
    if (msg != NULL) {
      free_clip_req(msg);
    }
  }
  if (json_obj != NULL) {
    json_object_put(json_obj);
  }
  return msg;
}
static uint8_t *format_clip_resp(struct clip_resp_s *clip_message_resp,
                                 uint32_t *size) {
  uint8_t *result = NULL;
  json_object *json_array = NULL;
  json_object *json_item = NULL;
  json_object *json_image = NULL;
  json_object *json_text = NULL;
  json_object *json_score = NULL;
  const char *json_str = NULL;
  size_t str_len = 0;
  int i = 0;

  if (clip_message_resp == NULL || size == NULL) {
    goto error;
  }

  json_array = json_object_new_array();
  if (json_array == NULL) {
    goto error;
  }

  for (i = 0; i < clip_message_resp->image_count; i++) {
    json_item = json_object_new_object();
    if (json_item == NULL) {
      goto error;
    }
    json_image = json_object_new_string(clip_message_resp->images[i]->image);
    json_object_object_add(json_item, "image", json_image);

    json_text = json_object_new_string(clip_message_resp->images[i]->text);
    json_object_object_add(json_item, "text", json_text);

    json_score = json_object_new_double(clip_message_resp->images[i]->score);
    json_object_object_add(json_item, "score", json_score);

    json_object_array_add(json_array, json_item);
    json_item = NULL;
  }

  json_str = json_object_to_json_string(json_array);
  if (json_str == NULL) {
    goto error;
  }

  str_len = strlen(json_str);
  result = (uint8_t *)malloc(str_len + 1);
  if (result == NULL) {
    goto error;
  }

  memcpy(result, json_str, str_len);
  result[str_len] = '\0';
  *size = str_len + 1;

  goto cleanup;

error:
  if (result != NULL) {
    free(result);
    result = NULL;
  }
  if (size != NULL) {
    *size = 0;
  }

cleanup:
  if (json_array != NULL) {
    json_object_put(json_array);
  }
  return result;
}

static int clip(struct option_s *opt, rknn_app_context_t *rknn_app_ctx,
                char **input_texts, const char *image_path,
                struct images *img) {
  int ret = -1;
  image_buffer_t src_image = {0};
  int text_lines = -1;
  clip_res out_res = {0};

  memset(&src_image, 0, sizeof(image_buffer_t));
  ret = read_image(image_path, &src_image);
  if (ret != 0) {
    printf("read image fail! ret=%d image_path=%s\n", ret, image_path);
    ret = -1;
    goto end;
  }

  ret = inference_clip_model(rknn_app_ctx, &src_image, input_texts, text_lines,
                             &out_res);
  if (ret != 0) {
    printf("inference_clip_model fail! ret=%d\n", ret);
    ret = -1;
    goto end;
  }
  strncpy(img->image, image_path, sizeof(img->image));
  strncpy(img->text, input_texts[out_res.text_index], sizeof(img->text));
  img->score = out_res.score;
  ret = 0;
end:
  if (src_image.virt_addr != NULL) {
    free(src_image.virt_addr);
  }
  return ret;
}

/*-------------------------------------------
                  Main Function
-------------------------------------------*/
int main(int argc, char **argv) {
  struct option_s opt = {0};
  struct bus_message_s *msg = NULL;
  struct clip_req_s *clip_req = NULL;
  struct clip_resp_s *clip_resp = NULL;
  int uds_client_sockfd = -1;
  char **input_texts = NULL;
  int text_lines = -1;
  int ret;
  rknn_app_context_t rknn_app_ctx = {0};
  uint32_t data_size = 0;
  uint8_t *buffer = NULL;

  parser_option(&opt, argc, argv);

  if (!strlen(opt.image_model_path) || !strlen(opt.text_model_path)) {
    help_guide(argv[0]);
    exit(0);
  }

  ret =
      init_clip_model(opt.image_model_path, opt.text_model_path, &rknn_app_ctx);
  if (ret != 0) {
    printf(
        "init_clip_model fail! ret=%d img_model_path=%s text_model_path=%s\n",
        ret, opt.image_model_path, opt.text_model_path);
    goto out;
  }
  input_texts = read_lines_from_file(opt.text_path, &text_lines);
  if (input_texts == NULL) {
    printf("read input texts fail! ret=%d text_path=%s\n", ret, opt.text_path);
    goto out;
  }

  uds_client_sockfd = uds_client_create(UDS_PATH);
  if (uds_client_sockfd < 0) {
    printf("uds_client_create fail! uds_path=%s\n", UDS_PATH);
    return -1;
  }

  msg = bus_message_new_from_register(UDS_CALL_TYPE_CLIP);  // 注册消息
  if (msg == NULL) {
    goto out;
  }
  if (send_bus_message(uds_client_sockfd, msg) < 0) {
    printf("Register message send fail!\n");
    goto out;
  }

  while (1) {
    msg = recv_bus_message(uds_client_sockfd, -1);
    if (msg == NULL) {
      goto end;
    }

    if (msg->magic != UDS_MESSAGE_MAGIC_INTERNAL) {
      goto end;
    }

    clip_req = parser_mgs_data(msg->msg_data);
    if (clip_resp == NULL) {
      goto end;
    }

    clip_resp = (struct clip_resp_s *)malloc(sizeof(struct clip_resp_s));
    if (clip_resp == NULL) {
      goto end;
    }

    for (size_t i = 0; i < clip_resp->image_count && i < MAX_IMAGE_COUNT; i++) {
      clip_resp->images[i] = (struct images *)malloc(sizeof(struct images));
      if (clip(&opt, &rknn_app_ctx, input_texts, clip_req->image_path[i],
               clip_resp->images[i]) != 0) {
        LOG("clip error");
        goto out;
      }
    }

    buffer = format_clip_resp(clip_resp, &data_size);
    if (buffer != NULL) {
      struct bus_message_s *resp_msg = bus_message_new_internal(
          uds_client_sockfd, msg->header.internal.src_fd, buffer, data_size);
      if (resp_msg) {
        send_bus_message(uds_client_sockfd, resp_msg);
        bus_message_free(resp_msg);
      }
    }
  end:
    if (msg) {
      bus_message_free(msg);
      msg = NULL;
    }
    if (clip_req) {
      free_clip_req(clip_req);
      clip_req = NULL;
    }
    if (clip_resp) {
      free_clip_resp(clip_resp);
      clip_resp = NULL;
    }
    if (buffer) {
      free(buffer);
      buffer = NULL;
    }
    sleep(1);
  }

out:
  ret = release_clip_model(&rknn_app_ctx);
  if (ret != 0) {
    printf("release_clip_model fail! ret=%d\n", ret);
  }

  if (input_texts != NULL) {
    free_lines(input_texts, text_lines);
  }

  if (uds_client_sockfd >= 0) {
    close(uds_client_sockfd);
  }
  if (clip_req) {
    free_clip_req(clip_req);
  }
  if (clip_resp) {
    free_clip_resp(clip_resp);
  }
  if (buffer) {
    free(buffer);
  }
  if (msg) {
    bus_message_free(msg);
  }

  return 0;
}
