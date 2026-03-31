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
  char clip_image_model_path[PATH_LEN + 1];
  char clip_txt_model_path[PATH_LEN + 1];
  char clip_labels_path[PATH_LEN + 1];

  char yolo8_model_path[PATH_LEN + 1];
  char yolo8_labels_path[PATH_LEN + 1];

  char uds_path[PATH_LEN + 1];

  uint8_t *data;
  uint32_t data_size;
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

struct scene_labels_s {
  int classId;
  char label_en[PATH_LEN];
  char label_cn[PATH_LEN];
  float probability;
};

struct object_labels_s {
  int classId;
  char label_en[PATH_LEN];
  char label_cn[PATH_LEN];
  int count;
  float *confidenceList;  // 动态数组，长度为count
};

struct faces_s {
  float *embedding;  // 动态数组，长度为embedding_size
  int embedding_size;
  char dominantEmotion_en[PATH_LEN];
  char dominantEmotion_cn[PATH_LEN];
};

struct clip_resp_s {
  char image[PATH_LEN];

  struct scene_labels_s *sceneLabels;
  int sceneLabelCount;

  struct object_labels_s *objectLabels;
  int objectLabelCount;

  struct faces_s *faces;
  int faceCount;
};

static struct clip_req_s *alloc_clip_req() {
  struct clip_req_s *msg =
      (struct clip_req_s *)malloc(sizeof(struct clip_req_s));
  if (msg == NULL) {
    LOG("Failed to allocate memory for clip_req");
    return NULL;
  }
  memset(msg, 0, sizeof(struct clip_req_s));
  return msg;
}

static void free_clip_req(struct clip_req_s *msg) {
  if (msg == NULL) return;

  for (int i = 0; i < msg->image_count; i++) {
    if (msg->image_path[i] != NULL) {
      free(msg->image_path[i]);
      msg->image_path[i] = NULL;
    }
  }
  free(msg);
}

static void dump_clip_req(struct clip_req_s *msg) {
  if (msg == NULL) {
    printf("clip_req is NULL\n");
    return;
  }

  printf("image_count: %d\n", msg->image_count);
  for (int i = 0; i < msg->image_count; i++) {
    if (msg->image_path[i] != NULL) {
      printf("image_path[%d]: %s\n", i, msg->image_path[i]);
    }
  }
}

static struct clip_resp_s *alloc_clip_resp() {
  struct clip_resp_s *resp =
      (struct clip_resp_s *)malloc(sizeof(struct clip_resp_s));
  if (resp == NULL) {
    LOG("Failed to allocate memory for clip_resp");
    return NULL;
  }
  memset(resp, 0, sizeof(struct clip_resp_s));
  return resp;
}
static void free_clip_resp(struct clip_resp_s *resp) {
  if (resp == NULL) return;

  // Free scene labels
  if (resp->sceneLabels != NULL) {
    free(resp->sceneLabels);
    resp->sceneLabels = NULL;
    resp->sceneLabelCount = 0;
  }

  // Free object labels and their confidence lists
  if (resp->objectLabels != NULL) {
    for (int i = 0; i < resp->objectLabelCount; i++) {
      if (resp->objectLabels[i].confidenceList != NULL) {
        free(resp->objectLabels[i].confidenceList);
        resp->objectLabels[i].confidenceList = NULL;
      }
    }
    free(resp->objectLabels);
    resp->objectLabels = NULL;
    resp->objectLabelCount = 0;
  }

  // Free faces and their embeddings
  if (resp->faces != NULL) {
    for (int i = 0; i < resp->faceCount; i++) {
      if (resp->faces[i].embedding != NULL) {
        free(resp->faces[i].embedding);
        resp->faces[i].embedding = NULL;
      }
    }
    free(resp->faces);
    resp->faces = NULL;
    resp->faceCount = 0;
  }

  // Free the response structure itself
  free(resp);
}
static void dump_clip_resp(struct clip_resp_s *resp) {
  if (resp == NULL) {
    printf("clip_resp is NULL\n");
    return;
  }

  printf("image: %s\n", resp->image);

  printf("sceneLabels count: %d\n", resp->sceneLabelCount);
  for (int i = 0; i < resp->sceneLabelCount; i++) {
    printf("  [%d] classId: %d, label_en: %s, label_cn: %s, probability: %f\n",
           i, resp->sceneLabels[i].classId, resp->sceneLabels[i].label_en,
           resp->sceneLabels[i].label_cn, resp->sceneLabels[i].probability);
  }

  printf("objectLabels count: %d\n", resp->objectLabelCount);
  for (int i = 0; i < resp->objectLabelCount; i++) {
    printf("  [%d] classId: %d, label_en: %s, label_cn: %s, count: %d\n", i,
           resp->objectLabels[i].classId, resp->objectLabels[i].label_en,
           resp->objectLabels[i].label_cn, resp->objectLabels[i].count);
    if (resp->objectLabels[i].confidenceList != NULL) {
      for (int j = 0; j < resp->objectLabels[i].count; j++) {
        printf("    confidence[%d]: %f\n", j,
               resp->objectLabels[i].confidenceList[j]);
      }
    }
  }

  printf("faces count: %d\n", resp->faceCount);
  for (int i = 0; i < resp->faceCount; i++) {
    printf(
        "  [%d] embedding_size: %d, dominantEmotion_en: %s, "
        "dominantEmotion_cn: %s\n",
        i, resp->faces[i].embedding_size, resp->faces[i].dominantEmotion_en,
        resp->faces[i].dominantEmotion_cn);
    if (resp->faces[i].embedding != NULL) {
      for (int j = 0; j < resp->faces[i].embedding_size; j++) {
        printf("    embedding[%d]: %f\n", j, resp->faces[i].embedding[j]);
      }
    }
  }
}

static void help_guide(char *prog_name) {
  printf("Usage: %s [OPTIONS] [ARGS]\n", prog_name);
  printf("Options:\n");
  printf("--clip-image-model-path,-i load clip image model path\n");
  printf("--clip-text-model-path,-t load clip text model path\n");
  printf("--clip-labels-path,-l load clip labels path\n");
  printf("--yolo8-model-path,-y load yolo8 model path\n");
  printf("--yolo8-labels-path,-Y load yolo8 labels path\n");
  printf("--unix-domain-socket,-u connect UDS\n");
  printf("--data load data\n");
  printf("--data-size load data size\n");
  printf("--help, -h  help guide\n");
}

static int parser_option(struct option_s *uopt, int argc, char **argv) {
  if (uopt == NULL || argc == 0 || argv == NULL) {
    LOG("Invalid parameters for parser_option");
    return -1;
  }

  int opt = 0;
  struct option longopt[] = {
      {"clip-image-model-path", required_argument, NULL, 'i'},
      {"clip-text-model-path", required_argument, NULL, 't'},
      {"clip-labels-path", required_argument, NULL, 'l'},
      {"yolo8-model-path", required_argument, NULL, 'y'},
      {"yolo8-labels-path", required_argument, NULL, 'Y'},
      {"unix-domain-socket", required_argument, NULL, 'u'},
      {"data", required_argument, NULL, 'd'},
      {"data-size", required_argument, NULL, 's'},
      {"help", no_argument, NULL, 'h'},
      {NULL, 0, NULL, 0}};

  while ((opt = getopt_long(argc, argv, "i:t:l:y:Y:u:d:s:h", longopt, NULL)) !=
         -1) {
    switch (opt) {
      case 'i': {
        if (optarg) {
          strncpy(uopt->clip_image_model_path, optarg,
                  sizeof(uopt->clip_image_model_path) - 1);
          uopt->clip_image_model_path[sizeof(uopt->clip_image_model_path) - 1] =
              '\0';
        }
      } break;
      case 't': {
        if (optarg) {
          strncpy(uopt->clip_txt_model_path, optarg,
                  sizeof(uopt->clip_txt_model_path) - 1);
          uopt->clip_txt_model_path[sizeof(uopt->clip_txt_model_path) - 1] =
              '\0';
        }
      } break;
      case 'l': {
        if (optarg) {
          strncpy(uopt->clip_labels_path, optarg,
                  sizeof(uopt->clip_labels_path) - 1);
          uopt->clip_labels_path[sizeof(uopt->clip_labels_path) - 1] = '\0';
        }
      } break;
      case 'y': {
        if (optarg) {
          strncpy(uopt->yolo8_model_path, optarg,
                  sizeof(uopt->yolo8_model_path) - 1);
          uopt->yolo8_model_path[sizeof(uopt->yolo8_model_path) - 1] = '\0';
        }
      } break;
      case 'Y': {
        if (optarg) {
          strncpy(uopt->yolo8_labels_path, optarg,
                  sizeof(uopt->yolo8_labels_path) - 1);
          uopt->yolo8_labels_path[sizeof(uopt->yolo8_labels_path) - 1] = '\0';
        }
      } break;
      case 'u': {
        if (optarg) {
          strncpy(uopt->uds_path, optarg, sizeof(uopt->uds_path) - 1);
          uopt->uds_path[sizeof(uopt->uds_path) - 1] = '\0';
        }
      } break;
      case 'd': {
        if (optarg) {
          int len = strlen(optarg);
          uopt->data = (uint8_t *)malloc(len + 1);
          if (uopt->data != NULL) {
            memcpy(uopt->data, optarg, len);
            uopt->data[len] = '\0';
            uopt->data_size = len + 1;
          } else {
            LOG("Failed to allocate memory for data");
            return -1;
          }
        }
      } break;
      case 's': {
        if (optarg) {
          uopt->data_size = atoi(optarg);
        }
      } break;
      case 'h': {
        help_guide(argv[0]);
        exit(0);
      } break;
      default: {
        help_guide(argv[0]);
        return -1;
      }
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
    LOG("Failed to allocate memory for clip_req_s");
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
        } else {
          LOG("Failed to allocate memory for image path[%d]", i);
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
  return ret;
}

static uint8_t *format_clip_resp(struct clip_resp_s *clip_message_resp,
                                 uint32_t *size) {
  uint8_t *result = NULL;
  json_object *json_array = NULL;
  json_object *json_item = NULL;
  json_object *json_scene_labels = NULL;
  json_object *json_object_labels = NULL;
  json_object *json_faces = NULL;
  json_object *json_label_obj = NULL;
  json_object *json_embedding_array = NULL;
  json_object *json_confidence_array = NULL;
  json_object *json_emotion_obj = NULL;
  const char *json_str = NULL;
  size_t str_len = 0;
  int i = 0, j = 0;

  if (clip_message_resp == NULL || size == NULL) {
    LOG("Invalid parameters for format_clip_resp");
    goto error;
  }

  json_array = json_object_new_array();
  if (json_array == NULL) {
    LOG("Failed to create JSON array");
    goto error;
  }

  json_item = json_object_new_object();
  if (json_item == NULL) {
    LOG("Failed to create JSON item");
    goto error;
  }

  // Add image path
  json_object_object_add(json_item, "image",
                         json_object_new_string(clip_message_resp->image));

  // Add scene labels
  json_scene_labels = json_object_new_array();
  for (i = 0; i < clip_message_resp->sceneLabelCount; i++) {
    json_object *scene_obj = json_object_new_object();
    json_object_object_add(
        scene_obj, "classId",
        json_object_new_int(clip_message_resp->sceneLabels[i].classId));

    json_label_obj = json_object_new_object();
    json_object_object_add(
        json_label_obj, "en",
        json_object_new_string(clip_message_resp->sceneLabels[i].label_en));
    json_object_object_add(
        json_label_obj, "cn",
        json_object_new_string(clip_message_resp->sceneLabels[i].label_cn));
    json_object_object_add(scene_obj, "label", json_label_obj);

    json_object_object_add(
        scene_obj, "probability",
        json_object_new_double(clip_message_resp->sceneLabels[i].probability));
    json_object_array_add(json_scene_labels, scene_obj);
  }
  json_object_object_add(json_item, "sceneLabels", json_scene_labels);

  // Add object labels
  json_object_labels = json_object_new_array();
  for (i = 0; i < clip_message_resp->objectLabelCount; i++) {
    json_object *obj = json_object_new_object();
    json_object_object_add(
        obj, "classId",
        json_object_new_int(clip_message_resp->objectLabels[i].classId));

    json_label_obj = json_object_new_object();
    json_object_object_add(
        json_label_obj, "en",
        json_object_new_string(clip_message_resp->objectLabels[i].label_en));
    json_object_object_add(
        json_label_obj, "cn",
        json_object_new_string(clip_message_resp->objectLabels[i].label_cn));
    json_object_object_add(obj, "label", json_label_obj);

    json_object_object_add(
        obj, "count",
        json_object_new_int(clip_message_resp->objectLabels[i].count));

    json_confidence_array = json_object_new_array();
    if (clip_message_resp->objectLabels[i].confidenceList != NULL) {
      for (j = 0; j < clip_message_resp->objectLabels[i].count; j++) {
        json_object_array_add(
            json_confidence_array,
            json_object_new_double(
                clip_message_resp->objectLabels[i].confidenceList[j]));
      }
    }
    json_object_object_add(obj, "confidenceList", json_confidence_array);
    json_object_array_add(json_object_labels, obj);
  }
  json_object_object_add(json_item, "objectLabels", json_object_labels);

  // Add faces
  json_faces = json_object_new_array();
  for (i = 0; i < clip_message_resp->faceCount; i++) {
    json_object *face_obj = json_object_new_object();

    json_embedding_array = json_object_new_array();
    if (clip_message_resp->faces[i].embedding != NULL) {
      for (j = 0; j < clip_message_resp->faces[i].embedding_size; j++) {
        json_object_array_add(
            json_embedding_array,
            json_object_new_double(clip_message_resp->faces[i].embedding[j]));
      }
    }
    json_object_object_add(face_obj, "embedding", json_embedding_array);

    json_emotion_obj = json_object_new_object();
    json_object_object_add(
        json_emotion_obj, "en",
        json_object_new_string(clip_message_resp->faces[i].dominantEmotion_en));
    json_object_object_add(
        json_emotion_obj, "cn",
        json_object_new_string(clip_message_resp->faces[i].dominantEmotion_cn));
    json_object_object_add(face_obj, "dominantEmotion", json_emotion_obj);

    json_object_array_add(json_faces, face_obj);
  }
  json_object_object_add(json_item, "faces", json_faces);

  json_object_array_add(json_array, json_item);

  json_str = json_object_to_json_string(json_array);
  if (json_str == NULL) {
    LOG("Failed to convert JSON to string");
    goto error;
  }

  str_len = strlen(json_str);
  result = (uint8_t *)malloc(str_len + 1);
  if (result == NULL) {
    LOG("Failed to allocate memory for result");
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
                char **input_texts, int text_num, const char *image_path,
                struct images *img) {
  if (opt == NULL || rknn_app_ctx == NULL || input_texts == NULL ||
      image_path == NULL || img == NULL) {
    LOG("Invalid parameters for clip");
    return -1;
  }

  int ret = -1;
  image_buffer_t src_image = {0};
  clip_res out_res = {0};

  memset(&src_image, 0, sizeof(image_buffer_t));
  ret = read_image(image_path, &src_image);
  if (ret != 0) {
    LOG("read image fail! ret=%d image_path=%s", ret, image_path);
    ret = -1;
    goto end;
  }

  ret = inference_clip_model(rknn_app_ctx, &src_image, input_texts, text_num,
                             &out_res);
  if (ret != 0) {
    LOG("inference_clip_model fail! ret=%d", ret);
    ret = -1;
    goto end;
  }

  strncpy(img->image, image_path, PATH_LEN - 1);
  img->image[PATH_LEN - 1] = '\0';
  strncpy(img->text, input_texts[out_res.text_index], PATH_LEN - 1);
  img->text[PATH_LEN - 1] = '\0';
  img->score = out_res.score;
  ret = 0;

end:
  if (src_image.virt_addr != NULL) {
    free(src_image.virt_addr);
  }
  return ret;
}

// 通用处理函数：处理请求并生成响应
static struct clip_resp_s *process_clip_request(
    struct clip_req_s *clip_req, struct option_s *opt,
    rknn_app_context_t *rknn_app_ctx, char **input_texts, int text_num) {
  if (clip_req == NULL || opt == NULL || rknn_app_ctx == NULL ||
      input_texts == NULL) {
    LOG("Invalid parameters for process_clip_request");
    return NULL;
  }

  // Initialize response
  struct clip_resp_s *clip_resp =
      (struct clip_resp_s *)malloc(sizeof(struct clip_resp_s));
  if (clip_resp == NULL) {
    LOG("Failed to allocate memory for clip_resp");
    return NULL;
  }
  memset(clip_resp, 0, sizeof(struct clip_resp_s));

  // Process each image
  int success_count = 0;
  for (int i = 0; i < clip_req->image_count && i < MAX_IMAGE_COUNT; i++) {
    struct images *img = (struct images *)malloc(sizeof(struct images));
    if (img == NULL) {
      LOG("Failed to allocate memory for image result at index %d", i);
      continue;  // Continue processing other images
    }

    if (clip(opt, rknn_app_ctx, input_texts, text_num, clip_req->image_path[i],
             img) != 0) {
      LOG("clip error for image: %s", clip_req->image_path[i]);
      free(img);
      continue;  // Continue processing other images
    }

    clip_resp->images[clip_resp->image_count++] = img;
    success_count++;
  }

  // If no successful processing, free the response
  if (success_count == 0) {
    free_clip_resp(clip_resp);
    return NULL;
  }

  return clip_resp;
}

// 发送响应的通用函数
static int send_response(int uds_client_sockfd,
                         struct bus_message_s *original_msg,
                         struct clip_resp_s *clip_resp) {
  if (uds_client_sockfd < 0 || original_msg == NULL || clip_resp == NULL) {
    LOG("Invalid parameters for send_response");
    return -1;
  }

  uint32_t data_size = 0;
  uint8_t *buffer = format_clip_resp(clip_resp, &data_size);
  if (buffer == NULL) {
    LOG("Failed to format response");
    return -1;
  }

  struct bus_message_s *resp_msg = bus_message_new_internal(
      uds_client_sockfd, original_msg->header.internal.src_fd, buffer,
      data_size);
  if (resp_msg) {
    int ret = send_bus_message(uds_client_sockfd, resp_msg);
    bus_message_free(resp_msg);
    free(buffer);
    return ret;
  } else {
    LOG("Failed to create response message");
    free(buffer);
    return -1;
  }
}

// 处理命令行数据并根据是否提供 UDS 路径决定发送方式
static int process_and_send_command_line_data(struct option_s *opt,
                                              rknn_app_context_t *rknn_app_ctx,
                                              char **input_texts, int text_num,
                                              int uds_client_sockfd) {
  if (opt == NULL || rknn_app_ctx == NULL || input_texts == NULL ||
      opt->data == NULL) {
    LOG("Invalid parameters for process_and_send_command_line_data");
    return -1;
  }

  LOG("Processing initial data from command line");
  struct clip_req_s *clip_req = parser_mgs_data(opt->data);
  if (clip_req == NULL) {
    LOG("Failed to parse command line data");
    return -1;
  }

  dump_clip_req(clip_req);

  // Validate image count
  if (clip_req->image_count <= 0 || clip_req->image_count > MAX_IMAGE_COUNT) {
    LOG("Invalid image count: %d", clip_req->image_count);
    free_clip_req(clip_req);
    return -1;
  }

  // Process the request
  struct clip_resp_s *clip_resp =
      process_clip_request(clip_req, opt, rknn_app_ctx, input_texts, text_num);
  if (clip_resp == NULL) {
    LOG("Failed to process command line request");
    free_clip_req(clip_req);
    return -1;
  }

  // Cleanup request data
  free_clip_req(clip_req);

  // 如果提供了 UDS 客户端，发送响应到 UDS 服务器
  if (uds_client_sockfd >= 0) {
    // 创建一个模拟的内部消息用于发送响应
    struct bus_message_s *initial_msg = bus_message_new_internal(
        uds_client_sockfd, uds_client_sockfd, opt->data, opt->data_size);
    if (initial_msg == NULL) {
      LOG("Failed to create initial message");
      free_clip_resp(clip_resp);
      return -1;
    }

    int ret = send_response(uds_client_sockfd, initial_msg, clip_resp);
    bus_message_free(initial_msg);
    free_clip_resp(clip_resp);
    return ret;
  } else {
    // 如果没有 UDS 客户端，打印结果
    uint32_t data_size = 0;
    uint8_t *buffer = format_clip_resp(clip_resp, &data_size);
    if (buffer != NULL) {
      printf("%s\n", (char *)buffer);
      free(buffer);
    }
    free_clip_resp(clip_resp);
    return 0;
  }
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
  int ret = 0;
  rknn_app_context_t rknn_app_ctx = {0};

  // Parse command line options
  if (parser_option(&opt, argc, argv) != 0) {
    LOG("Failed to parse options");
    ret = -1;
    goto out;
  }

  if (!strlen(opt.image_model_path) || !strlen(opt.text_model_path)) {
    help_guide(argv[0]);
    ret = -1;
    goto out;
  }

  // Initialize CLIP model
  ret =
      init_clip_model(opt.image_model_path, opt.text_model_path, &rknn_app_ctx);
  if (ret != 0) {
    LOG("init_clip_model fail! ret=%d img_model_path=%s text_model_path=%s",
        ret, opt.image_model_path, opt.text_model_path);
    goto out;
  }

  // Load input texts
  input_texts = read_lines_from_file(opt.text_path, &text_lines);
  if (input_texts == NULL || text_lines <= 0) {
    LOG("read input texts fail! text_path=%s lines=%d", opt.text_path,
        text_lines);
    ret = -1;
    goto out;
  }

  if (strlen(opt.uds_path)) {
    uds_client_sockfd = uds_client_create(opt.uds_path);
    if (uds_client_sockfd < 0) {
      LOG("uds_client_create fail! uds_path=%s", opt.uds_path);
      ret = -1;
      goto out;
    }
    // Register with UDS server
    msg = bus_message_new_from_register(UDS_CALL_TYPE_CLIP);
    if (msg == NULL) {
      LOG("Failed to create register message");
      ret = -1;
      goto out;
    }

    if (send_bus_message(uds_client_sockfd, msg) < 0) {
      LOG("Register message send fail!");
      bus_message_free(msg);
      ret = -1;
      goto out;
    }
    bus_message_free(msg);
    msg = NULL;
  } else {
    LOG("No UDS path provided, running in standalone mode");
  }

  if (opt.data) {
    ret = process_and_send_command_line_data(&opt, &rknn_app_ctx, input_texts,
                                             text_lines, uds_client_sockfd);
    if (ret != 0) {
      LOG("Failed to process and send command line data");
      ret = -1;
      goto out;
    }
  }

  // Main event loop
  while (1) {
    // Receive message from UDS server
    msg = recv_bus_message(uds_client_sockfd, -1);
    if (msg == NULL) {
      LOG("Failed to receive message from UDS server");
      continue;  // Keep running
    }

    // Validate message type
    if (msg->magic != UDS_MESSAGE_MAGIC_INTERNAL) {
      LOG("Invalid message magic: %d", msg->magic);
      bus_message_free(msg);
      continue;  // Keep running
    }

    // Parse request data
    clip_req = parser_mgs_data(msg->msg_data);
    if (clip_req == NULL) {
      LOG("Failed to parse message data");
      bus_message_free(msg);
      continue;  // Keep running
    }

    // Validate image count
    if (clip_req->image_count <= 0 || clip_req->image_count > MAX_IMAGE_COUNT) {
      LOG("Invalid image count: %d", clip_req->image_count);
      free_clip_req(clip_req);
      bus_message_free(msg);
      continue;  // Keep running
    }

    // Process the request using the common function
    clip_resp = process_clip_request(clip_req, &opt, &rknn_app_ctx, input_texts,
                                     text_lines);
    if (clip_resp != NULL) {
      // Send response using the common function
      send_response(uds_client_sockfd, msg, clip_resp);
      free_clip_resp(clip_resp);
      clip_resp = NULL;
    } else {
      LOG("Failed to process request or no successful results");
    }

    // Cleanup for this iteration
    if (msg) {
      bus_message_free(msg);
      msg = NULL;
    }
    if (clip_req) {
      free_clip_req(clip_req);
      clip_req = NULL;
    }
  }
out:
  // Cleanup resources
  if (ret == 0) {
    ret = release_clip_model(&rknn_app_ctx);
    if (ret != 0) {
      LOG("release_clip_model fail! ret=%d", ret);
    }
  }

  if (input_texts != NULL) {
    free_lines(input_texts, text_lines);
  }

  if (uds_client_sockfd >= 0) {
    close(uds_client_sockfd);
  }

  // Free any remaining resources
  if (clip_req) {
    free_clip_req(clip_req);
  }
  if (clip_resp) {
    free_clip_resp(clip_resp);
  }
  if (msg) {
    bus_message_free(msg);
  }
  if (opt.data) {
    free(opt.data);
  }

  return ret;
}