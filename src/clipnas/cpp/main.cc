// Copyright (c) 2024 by Rockchip Electronics Co., Ltd. All Rights Reserved.
// Licensed under the Apache License, Version 2.0
// -------------------------------------------

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include "clip.h"
#include "file_utils.h"
#include "image_utils.h"
#include "json-c/json.h"
#include "rw_labels.h"
#include "uds_bus.h"
#include "yolov8.h"

#define PATH_LEN 256
#define LOG(fmt, ...) printf("[%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define SAFE_FREE(p) do { if (p) { free(p); (p) = NULL; } } while(0)

/*-------------------------------------------
                Structs
-------------------------------------------*/
struct rknn_clip_t {
    rknn_app_context_t app_ctx;
    struct clip_labelset_s labelset;
};

struct rknn_yolov8_t {
    rknn_yolov8_app_context_t yolov8_app_ctx;
    struct yolo8_labelset_s labelset;
};

struct option_s {
    char clip_image_model_path[PATH_LEN + 1];
    char clip_txt_model_path[PATH_LEN + 1];
    char clip_labels_path[PATH_LEN + 1];
    char yolo8_model_path[PATH_LEN + 1];
    char yolo8_labels_path[PATH_LEN + 1];
    char uds_path[PATH_LEN + 1];
    int src_fd;
    uint8_t *data;
    uint32_t data_size;
};

struct context_s {
    struct rknn_yolov8_t yolov8;
    struct rknn_clip_t clip;
    struct option_s option;
};

struct clip_req_s {
    int image_count;
    char *image_path[MAX_IMAGE_COUNT];
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
    float *confidenceList;
};

struct faces_s {
    float *embedding;
    int embedding_size;
    char dominantEmotion_en[PATH_LEN];
    char dominantEmotion_cn[PATH_LEN];
};

struct img_res_s {
    char image[PATH_LEN];
    struct scene_labels_s *sceneLabels;
    int sceneLabelCount;
    struct object_labels_s *objectLabels;
    int objectLabelCount;
    struct faces_s *faces;
    int faceCount;
};

struct clip_resp_s {
    struct img_res_s *img_ress;
    int img_count;
};

/*-------------------------------------------
        Forward Declarations
-------------------------------------------*/
static struct clip_req_s *alloc_clip_req(void);
static void free_clip_req(struct clip_req_s *msg);
static struct clip_resp_s *alloc_clip_resp(void);
static void free_img_res(struct img_res_s *img_res);
static void free_clip_resp(struct clip_resp_s *resp);
static uint8_t *format_clip_resp(struct clip_resp_s *clip_message_resp, uint32_t *size);

/*-------------------------------------------
        Request / Response Utils
-------------------------------------------*/
static struct clip_req_s *alloc_clip_req(void) {
    struct clip_req_s *msg = (struct clip_req_s *)calloc(1, (size_t)sizeof(struct clip_req_s));
    if (!msg) {
        LOG("calloc failed");
        return NULL;
    }
    return msg;
}

static void free_clip_req(struct clip_req_s *msg) {
    int i;
    if (!msg) return;
    for (i = 0; i < msg->image_count; i++) {
        SAFE_FREE(msg->image_path[i]);
    }
    SAFE_FREE(msg);
}

static void dump_clip_req(struct clip_req_s *msg) {
    int i;
    if (!msg) {
        printf("clip_req is NULL\n");
        return;
    }
    printf("image_count: %d\n", msg->image_count);
    for (i = 0; i < msg->image_count; i++) {
        if (msg->image_path[i]) {
            printf("image_path[%d]: %s\n", i, msg->image_path[i]);
        }
    }
}

static struct clip_resp_s *alloc_clip_resp(void) {
    struct clip_resp_s *resp = (struct clip_resp_s *)calloc(1, (size_t)sizeof(struct clip_resp_s));
    if (!resp) {
        LOG("calloc failed");
        return NULL;
    }
    return resp;
}

static void free_img_res(struct img_res_s *img_res) {
    int i;
    if (!img_res) return;

    SAFE_FREE(img_res->sceneLabels);
    img_res->sceneLabelCount = 0;

    for (i = 0; i < img_res->objectLabelCount; i++) {
        SAFE_FREE(img_res->objectLabels[i].confidenceList);
    }
    SAFE_FREE(img_res->objectLabels);
    img_res->objectLabelCount = 0;

    for (i = 0; i < img_res->faceCount; i++) {
        SAFE_FREE(img_res->faces[i].embedding);
    }
    SAFE_FREE(img_res->faces);
    img_res->faceCount = 0;
    memset(img_res, 0, (size_t)sizeof(struct img_res_s));
}

static void free_clip_resp(struct clip_resp_s *resp) {
    int i;
    if (!resp) return;
    if (resp->img_ress) {
        for (i = 0; i < resp->img_count; i++) {
            free_img_res(&resp->img_ress[i]);
        }
        SAFE_FREE(resp->img_ress);
    }
    resp->img_count = 0;
    SAFE_FREE(resp);
}

static void dump_clip_resp(struct clip_resp_s *resp) {
    int k, i;
    if (!resp) {
        printf("clip_resp is NULL\n");
        return;
    }
    printf("img_count: %d\n", resp->img_count);
    for (k = 0; k < resp->img_count; k++) {
        struct img_res_s *img_res = &resp->img_ress[k];
        printf("\n=== Image %d: %s ===\n", k, img_res->image);
        printf("sceneLabels: %d\n", img_res->sceneLabelCount);
        printf("objectLabels: %d\n", img_res->objectLabelCount);
    }
}

/*-------------------------------------------
        Option Parse
-------------------------------------------*/
static void help_guide(char *prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("  -i clip-image-model-path\n");
    printf("  -t clip-text-model-path\n");
    printf("  -l clip-labels-path\n");
    printf("  -y yolo8-model-path\n");
    printf("  -Y yolo8-labels-path\n");
    printf("  -u unix-domain-socket\n");
    printf("  -d data\n");
    printf("  -s data-size\n");
    printf("  -f data-source-fd\n");
    printf("  -h help\n");
}

static int parser_option(struct option_s *uopt, int argc, char **argv) {
    int opt = 0;
    int len = 0;
    if (!uopt || !argv) {
        return -1;
    }
    memset(uopt, 0, (size_t)sizeof(struct option_s));
    uopt->src_fd = -1;

    struct option long_opts[] = {
        {"clip-image-model-path", required_argument, NULL, 'i'},
        {"clip-text-model-path",  required_argument, NULL, 't'},
        {"clip-labels-path",      required_argument, NULL, 'l'},
        {"yolo8-model-path",      required_argument, NULL, 'y'},
        {"yolo8-labels-path",     required_argument, NULL, 'Y'},
        {"unix-domain-socket",    required_argument, NULL, 'u'},
        {"data",                  required_argument, NULL, 'd'},
        {"data-size",             required_argument, NULL, 's'},
        {"data-source-fd",        required_argument, NULL, 'f'},
        {"help",                  no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "i:t:l:y:Y:u:d:s:f:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'i':
                strncpy(uopt->clip_image_model_path, optarg, (size_t)PATH_LEN);
                uopt->clip_image_model_path[PATH_LEN] = '\0';
                break;
            case 't':
                strncpy(uopt->clip_txt_model_path, optarg, (size_t)PATH_LEN);
                uopt->clip_txt_model_path[PATH_LEN] = '\0';
                break;
            case 'l':
                strncpy(uopt->clip_labels_path, optarg, (size_t)PATH_LEN);
                uopt->clip_labels_path[PATH_LEN] = '\0';
                break;
            case 'y':
                strncpy(uopt->yolo8_model_path, optarg, (size_t)PATH_LEN);
                uopt->yolo8_model_path[PATH_LEN] = '\0';
                break;
            case 'Y':
                strncpy(uopt->yolo8_labels_path, optarg, (size_t)PATH_LEN);
                uopt->yolo8_labels_path[PATH_LEN] = '\0';
                break;
            case 'u':
                strncpy(uopt->uds_path, optarg, (size_t)PATH_LEN);
                uopt->uds_path[PATH_LEN] = '\0';
                break;
            case 'd':
                len = (int)strlen(optarg);
                uopt->data = (uint8_t *)malloc((size_t)(len + 1));
                if (!uopt->data) return -1;
                memcpy(uopt->data, (const uint8_t *)optarg, (size_t)len);
                uopt->data[len] = '\0';
                uopt->data_size = (uint32_t)(len + 1);
                break;
            case 's':
                uopt->data_size = (uint32_t)atoi(optarg);
                break;
            case 'f':
                uopt->src_fd = atoi(optarg);
                break;
            case 'h':
                help_guide(argv[0]);
                exit(0);
            default:
                help_guide(argv[0]);
                return -1;
        }
    }
    return 0;
}

/*-------------------------------------------
        JSON Parse Request
-------------------------------------------*/
static struct clip_req_s *parser_mgs_data(uint8_t *data) {
    json_object *obj = NULL;
    json_object *item = NULL;
    const char *path = NULL;
    int n = 0;
    int i = 0;
    struct clip_req_s *req = NULL;

    if (!data) {
        LOG("null data");
        return NULL;
    }

    obj = json_tokener_parse((char *)data);
    if (!obj) {
        LOG("json parse fail");
        return NULL;
    }

    if (!json_object_is_type(obj, json_type_array)) {
        LOG("not array");
        json_object_put(obj);
        return NULL;
    }

    req = alloc_clip_req();
    if (!req) {
        json_object_put(obj);
        return NULL;
    }

    n = json_object_array_length(obj);
    if (n < 0 || n > MAX_IMAGE_COUNT) {
        LOG("invalid count");
        json_object_put(obj);
        free_clip_req(req);
        return NULL;
    }

    for (i = 0; i < n; i++) {
        item = json_object_array_get_idx(obj, i);
        if (!item || !json_object_is_type(item, json_type_string)) continue;
        path = json_object_get_string(item);
        if (!path) continue;
        req->image_path[i] = (char *)strdup(path);
        if (req->image_path[i]) {
            req->image_count++;
        }
    }

    json_object_put(obj);
    dump_clip_req(req);
    return req;
}

/*-------------------------------------------
        JSON Format Response
-------------------------------------------*/
static uint8_t *format_clip_resp(struct clip_resp_s *clip_message_resp, uint32_t *size) {
    json_object *array = NULL;
    json_object *item = NULL;
    json_object *ja_scene = NULL;
    json_object *ja_obj = NULL;
    json_object *ja_face = NULL;
    json_object *o = NULL;
    json_object *lab = NULL;
    const char *json_str = NULL;
    uint8_t *buf = NULL;
    int len = 0;
    int k = 0;
    int i = 0;
    int j = 0;

    *size = 0;
    if (!clip_message_resp) return NULL;

    array = json_object_new_array();
    if (!array) return NULL;

    for (k = 0; k < clip_message_resp->img_count; k++) {
        struct img_res_s *im = &clip_message_resp->img_ress[k];
        item = json_object_new_object();
        json_object_object_add(item, "image", json_object_new_string(im->image));

        ja_scene = json_object_new_array();
        for (i = 0; i < im->sceneLabelCount; i++) {
            o = json_object_new_object();
            json_object_object_add(o, "classId", json_object_new_int((int)im->sceneLabels[i].classId));
            lab = json_object_new_object();
            json_object_object_add(lab, "en", json_object_new_string(im->sceneLabels[i].label_en));
            json_object_object_add(lab, "cn", json_object_new_string(im->sceneLabels[i].label_cn));
            json_object_object_add(o, "label", lab);
            json_object_object_add(o, "probability", json_object_new_double((double)im->sceneLabels[i].probability));
            json_object_array_add(ja_scene, o);
        }
        json_object_object_add(item, "sceneLabels", ja_scene);

        ja_obj = json_object_new_array();
        for (i = 0; i < im->objectLabelCount; i++) {
            o = json_object_new_object();
            json_object_object_add(o, "classId", json_object_new_int((int)im->objectLabels[i].classId));
            lab = json_object_new_object();
            json_object_object_add(lab, "en", json_object_new_string(im->objectLabels[i].label_en));
            json_object_object_add(lab, "cn", json_object_new_string(im->objectLabels[i].label_cn));
            json_object_object_add(o, "label", lab);
            json_object_object_add(o, "count", json_object_new_int((int)im->objectLabels[i].count));
            json_object *ja_conf = json_object_new_array();
            for (j = 0; j < im->objectLabels[i].count; j++) {
                json_object_array_add(ja_conf, json_object_new_double((double)im->objectLabels[i].confidenceList[j]));
            }
            json_object_object_add(o, "confidenceList", ja_conf);
            json_object_array_add(ja_obj, o);
        }
        json_object_object_add(item, "objectLabels", ja_obj);

        ja_face = json_object_new_array();
        json_object_object_add(item, "faces", ja_face);
        json_object_array_add(array, item);
    }

    json_str = json_object_to_json_string(array);
    if (!json_str) {
        json_object_put(array);
        return NULL;
    }

    len = (int)strlen(json_str);
    buf = (uint8_t *)malloc((size_t)(len + 1));
    if (buf) {
        memcpy(buf, (const uint8_t *)json_str, (size_t)len);
        buf[len] = '\0';
        *size = (uint32_t)(len + 1);
    }

    json_object_put(array);
    return buf;
}

/*-------------------------------------------
        CLIP Text Prepare
-------------------------------------------*/
static void free_clip_input_txts(char **input_texts, int text_num) {
    int i = 0;
    if (!input_texts) return;
    for (i = 0; i < text_num; i++) {
        SAFE_FREE(input_texts[i]);
    }
    SAFE_FREE(input_texts);
}

static int gen_clip_input_txts(struct context_s *ctx, char ***input_texts, int *text_num) {
    int i = 0;
    int j = 0;
    int cnt = 0;
    char **texts = NULL;
    char **tmp = NULL;
    char *text = NULL;

    *input_texts = NULL;
    *text_num = 0;

    if (!ctx) return -1;

    for (i = 0; i < ctx->clip.labelset.filter_count; i++) {
        filter *f = &ctx->clip.labelset.filters[i];
        for (j = 0; j < f->category_count; j++) {
            category *cat = &f->categories[j];
            if (cat->clipText[0] == 0) continue;

            tmp = (char **)realloc(texts, (size_t)((cnt + 1) * sizeof(char *)));
            if (!tmp) goto err;
            texts = tmp;

            text = (char *)strdup(cat->clipText);
            if (!text) goto err;
            texts[cnt] = text;
            cnt++;
        }
    }

    *input_texts = texts;
    *text_num = cnt;
    return 0;

err:
    free_clip_input_txts(texts, cnt);
    return -1;
}

static category *find_clip_label_by_clip_text_index(struct context_s *ctx, char *text, int text_index, int *filter_index) {
    int i = 0;
    int j = 0;
    int cur = 0;

    if (!ctx || !text) return NULL;

    for (i = 0; i < ctx->clip.labelset.filter_count; i++) {
        filter *f = &ctx->clip.labelset.filters[i];
        for (j = 0; j < f->category_count; j++) {
            category *cat = &f->categories[j];
            if (cat->clipText[0] == 0) continue;
            if (cur == text_index && !strcmp(cat->clipText, text)) {
                if (filter_index) *filter_index = i;
                return cat;
            }
            cur++;
        }
    }
    return NULL;
}

/*-------------------------------------------
        Result Fill
-------------------------------------------*/
static void copy_labelresult(struct context_s *ctx, char **input_texts, clip_res *out_res,
                             object_detect_result_list *yolo_res, struct img_res_s *img_res) {
    int filter_idx = 0;
    category *cat = NULL;
    filter *f = NULL;
    const char *type = NULL;
    void *tmp = NULL;
    struct scene_labels_s *s = NULL;
    struct object_labels_s *o = NULL;
    int i = 0;
    int cid = 0;
    float score = 0.0f;
    int found = 0;
    float *newc = NULL;
    const char *name = NULL;

    if (!ctx || !out_res || !img_res || !input_texts) return;

    cat = find_clip_label_by_clip_text_index(ctx, input_texts[out_res->text_index],
                                              out_res->text_index, &filter_idx);
    if (!cat) return;

    f = &ctx->clip.labelset.filters[filter_idx];
    type = f->type;

    if (!cat->useYolo && !cat->useScene) {
        if (!strcmp(type, "scene")) {
            tmp = realloc(img_res->sceneLabels, (size_t)((img_res->sceneLabelCount + 1) * sizeof(struct scene_labels_s)));
            if (!tmp) return;
            img_res->sceneLabels = (struct scene_labels_s *)tmp;
            s = &img_res->sceneLabels[img_res->sceneLabelCount];
            memset(s, 0, (size_t)sizeof(*s));
            s->classId = (int)cat->label.classId;
            strncpy(s->label_en, cat->label.labelName.en, (size_t)(PATH_LEN - 1));
            strncpy(s->label_cn, cat->label.labelName.cn, (size_t)(PATH_LEN - 1));
            s->probability = (float)out_res->score;
            img_res->sceneLabelCount++;
        } else if (!strcmp(type, "object")) {
            tmp = realloc(img_res->objectLabels, (size_t)((img_res->objectLabelCount + 1) * sizeof(struct object_labels_s)));
            if (!tmp) return;
            img_res->objectLabels = (struct object_labels_s *)tmp;
            o = &img_res->objectLabels[img_res->objectLabelCount];
            memset(o, 0, (size_t)sizeof(*o));
            o->classId = (int)cat->label.classId;
            strncpy(o->label_en, cat->label.labelName.en, (size_t)(PATH_LEN - 1));
            strncpy(o->label_cn, cat->label.labelName.cn, (size_t)(PATH_LEN - 1));
            o->count = 1;
            o->confidenceList = (float *)malloc((size_t)sizeof(float));
            if (o->confidenceList) o->confidenceList[0] = (float)out_res->score;
            img_res->objectLabelCount++;
        }
    } else {
        if (cat->useYolo && yolo_res) {
            for (i = 0; i < yolo_res->count; i++) {
                cid = (int)yolo_res->results[i].cls_id;
                score = (float)yolo_res->results[i].prop;
                found = -1;

                for (int j = 0; j < img_res->objectLabelCount; j++) {
                    if (img_res->objectLabels[j].classId == cid) {
                        found = j;
                        break;
                    }
                }

                if (found >= 0) {
                    newc = (float *)realloc(img_res->objectLabels[found].confidenceList,
                            (size_t)((img_res->objectLabels[found].count + 1) * sizeof(float)));
                    if (!newc) continue;
                    img_res->objectLabels[found].confidenceList = newc;
                    img_res->objectLabels[found].confidenceList[img_res->objectLabels[found].count] = score;
                    img_res->objectLabels[found].count++;
                } else {
                    tmp = realloc(img_res->objectLabels,
                            (size_t)((img_res->objectLabelCount + 1) * sizeof(struct object_labels_s)));
                    if (!tmp) continue;
                    img_res->objectLabels = (struct object_labels_s *)tmp;
                    o = &img_res->objectLabels[img_res->objectLabelCount];
                    memset(o, 0, (size_t)sizeof(*o));
                    o->classId = cid;
                    o->count = 1;
                    o->confidenceList = (float *)malloc((size_t)sizeof(float));
                    if (o->confidenceList) o->confidenceList[0] = score;

                    name = coco_cls_to_name(cid);
                    if (name) {
                        strncpy(o->label_en, name, (size_t)(PATH_LEN - 1));
                        strncpy(o->label_cn, name, (size_t)(PATH_LEN - 1));
                    } else {
                        snprintf(o->label_en, (size_t)PATH_LEN, "class_%d", cid);
                        snprintf(o->label_cn, (size_t)PATH_LEN, "类别_%d", cid);
                    }
                    img_res->objectLabelCount++;
                }
            }
        }
    }
}

/*-------------------------------------------
        Single Image Infer
-------------------------------------------*/
static int img_label_handler(struct context_s *ctx, const char *image_path, struct img_res_s *img_res) {
    int ret = -1;
    image_buffer_t src = {0};
    char **texts = NULL;
    int text_num = 0;
    clip_res out_res = {0};
    object_detect_result_list yolo_res = {0};
    category *cat = NULL;

    if (!ctx || !image_path || !img_res) {
        return -1;
    }

    if (read_image(image_path, &src) != 0) {
        LOG("read image fail");
        goto end;
    }

    if (gen_clip_input_txts(ctx, &texts, &text_num) != 0) {
        LOG("gen text fail");
        goto end;
    }

    if (inference_clip_model(&ctx->clip.app_ctx, &src, texts, text_num, &out_res) != 0) {
        LOG("clip infer fail");
        goto end;
    }

    cat = find_clip_label_by_clip_text_index(ctx, texts[out_res.text_index], out_res.text_index, NULL);
    if (!cat) {
        LOG("no cat");
        goto end;
    }

    strncpy(img_res->image, image_path, (size_t)(PATH_LEN - 1));
    img_res->image[PATH_LEN - 1] = '\0';

    if (cat->useYolo) {
        inference_yolov8_model(&ctx->yolov8.yolov8_app_ctx, &src, &yolo_res);
    }

    copy_labelresult(ctx, texts, &out_res, &yolo_res, img_res);
    ret = 0;

end:
    SAFE_FREE(src.virt_addr);
    free_clip_input_txts(texts, text_num);
    return ret;
}

/*-------------------------------------------
        Process Batch Images
-------------------------------------------*/
static struct clip_resp_s *process_clip_request(struct clip_req_s *clip_req, struct context_s *ctx) {
    int i = 0;
    int ok = 0;
    struct clip_resp_s *resp = NULL;

    if (!clip_req || !ctx || clip_req->image_count <= 0) {
        return NULL;
    }

    resp = alloc_clip_resp();
    if (!resp) return NULL;

    resp->img_count = clip_req->image_count;
    resp->img_ress = (struct img_res_s *)calloc((size_t)resp->img_count, (size_t)sizeof(struct img_res_s));
    if (!resp->img_ress) {
        free_clip_resp(resp);
        return NULL;
    }

    for (i = 0; i < clip_req->image_count; i++) {
        if (img_label_handler(ctx, clip_req->image_path[i], &resp->img_ress[i]) == 0) {
            ok++;
        }
    }

    if (ok == 0) {
        free_clip_resp(resp);
        return NULL;
    }

    return resp;
}

/*-------------------------------------------
        Send Response
-------------------------------------------*/
static int send_response(int sock, struct bus_message_s *orig, struct clip_resp_s *resp) {
    int ret = -1;
    uint32_t sz = 0;
    uint8_t *buf = NULL;
    struct bus_message_s *msg = NULL;

    if (sock < 0 || !orig || !resp) {
        return -1;
    }

    buf = format_clip_resp(resp, &sz);
    if (!buf) {
        return -1;
    }

    msg = bus_message_new_internal(sock, orig->header.internal.src_fd, buf, sz);
    if (msg) {
        ret = send_bus_message(sock, msg);
        bus_message_free(msg);
    }

    SAFE_FREE(buf);
    return ret;
}

/*-------------------------------------------
        CLI Mode
-------------------------------------------*/
static int process_and_send_command_line_data(struct context_s *ctx, int sock) {
    int ret = -1;
    struct clip_req_s *req = NULL;
    struct clip_resp_s *resp = NULL;
    uint8_t *buf = NULL;
    uint32_t sz = 0;
    struct bus_message_s *m = NULL;

    if (!ctx || !ctx->option.data) {
        return -1;
    }

    req = parser_mgs_data(ctx->option.data);
    if (!req) {
        goto end;
    }

    resp = process_clip_request(req, ctx);
    if (!resp) {
        goto end;
    }

    buf = format_clip_resp(resp, &sz);
    if (buf) {
        if (sock >= 0) {
            m = bus_message_new_internal(sock, ctx->option.src_fd, buf, sz);
            if (m) {
                send_response(sock, m, resp);
                bus_message_free(m);
            }
        } else {
            printf("%s\n", (char *)buf);
        }
    }

    ret = 0;

end:
    SAFE_FREE(buf);
    if (resp) free_clip_resp(resp);
    if (req) free_clip_req(req);
    return ret;
}

/*-------------------------------------------
        Main
-------------------------------------------*/
int main(int argc, char **argv) {
    int ret = 0;
    struct context_s ctx = {0};
    int uds_fd = -1;
    struct bus_message_s *msg = NULL;
    struct clip_req_s *clip_req = NULL;
    struct clip_resp_s *clip_resp = NULL;
    struct bus_message_s *reg = NULL;

    ret = parser_option(&ctx.option, argc, argv);
    if (ret != 0) {
        help_guide(argv[0]);
        goto out;
    }

    if (!strlen(ctx.option.clip_image_model_path) ||
        !strlen(ctx.option.clip_txt_model_path) ||
        !strlen(ctx.option.yolo8_model_path) ||
        !strlen(ctx.option.clip_labels_path) ||
        !strlen(ctx.option.yolo8_labels_path)) {
        help_guide(argv[0]);
        ret = -1;
        goto out;
    }

    ret = init_clip_model(ctx.option.clip_image_model_path,
                          ctx.option.clip_txt_model_path,
                          &ctx.clip.app_ctx);
    if (ret != 0) {
        LOG("init clip fail");
        goto out;
    }

    ret = init_yolov8_model(ctx.option.yolo8_model_path, &ctx.yolov8.yolov8_app_ctx);
    if (ret != 0) {
        LOG("init yolo fail");
        goto out;
    }

    read_clip_labels(&ctx.clip.labelset, ctx.option.clip_labels_path);
    read_yolo8_labels(&ctx.yolov8.labelset, ctx.option.yolo8_labels_path);

    if (ctx.clip.labelset.filter_count <= 0 || ctx.yolov8.labelset.label_count <= 0) {
        LOG("load labels fail");
        ret = -1;
        goto out;
    }

    if (strlen(ctx.option.uds_path)) {
        uds_fd = uds_client_create(ctx.option.uds_path);
        if (uds_fd < 0) {
            LOG("uds create fail");
            ret = -1;
            goto out;
        }

        reg = bus_message_new_from_register(UDS_CALL_TYPE_CLIP);
        if (reg) {
            send_bus_message(uds_fd, reg);
            bus_message_free(reg);
        }
    }

    if (ctx.option.data && ctx.option.data_size > 0 && ctx.option.src_fd >= 0) {
        process_and_send_command_line_data(&ctx, uds_fd);
    }

    if (uds_fd >= 0) {
        while (1) {
            msg = recv_bus_message(uds_fd, -1);
            if (!msg) continue;

            if (msg->magic != UDS_MESSAGE_MAGIC_INTERNAL) {
                bus_message_free(msg);
                continue;
            }

            clip_req = parser_mgs_data(msg->msg_data);
            if (clip_req) {
                clip_resp = process_clip_request(clip_req, &ctx);
                if (clip_resp) {
                    send_response(uds_fd, msg, clip_resp);
                    free_clip_resp(clip_resp);
                    clip_resp = NULL;
                }
                free_clip_req(clip_req);
                clip_req = NULL;
            }
            bus_message_free(msg);
            msg = NULL;
        }
    }

out:
    free_clip_labelset(&ctx.clip.labelset);
    free_yolo8_labelset(&ctx.yolov8.labelset);
    release_clip_model(&ctx.clip.app_ctx);
    release_yolov8_model(&ctx.yolov8.yolov8_app_ctx);
    if (uds_fd >= 0) close(uds_fd);
    SAFE_FREE(ctx.option.data);
    return ret;
}