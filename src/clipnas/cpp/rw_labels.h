#ifndef __RW_LABELS_H__
#define __RW_LABELS_H__
#include <stdio.h>
#include <stdlib.h>

#include "json-c/json.h"

struct yolo8_label_s {
  int class_id;
  char label_en[256];
  char label_cn[256];
};

struct yolo8_labelset_s {
  struct yolo8_label_s *labels;
  int label_count;
};

#define MAX_LABEL_CN       64
#define MAX_LABEL_EN       64
#define MAX_TEXT_LEN       512
#define MAX_NAME_LEN       128
typedef struct {
    char cn[MAX_LABEL_CN];
    char en[MAX_LABEL_EN];
} labelname;

typedef struct {
    int classId;
    labelname labelName;
} label;

typedef struct {
    int has_label;
    int useFace;
    int useYolo;
    int useScene;

    label label;
    char clipText[MAX_TEXT_LEN];
} category;

typedef struct {
    char name[MAX_NAME_LEN];
    char type[MAX_NAME_LEN];
    float threshold;

    category *categories;
    int category_count;
} filter;

struct clip_labelset_s {
    filter *filters;
    int filter_count;
};

void free_yolo8_labelset(struct yolo8_labelset_s *labelset);
void free_clip_labelset(struct clip_labelset_s *labelset);

int read_yolo8_labels(struct yolo8_labelset_s *labelset, const char *file_path);
int read_clip_labels(struct clip_labelset_s *labelset, const char *file_path);

#endif