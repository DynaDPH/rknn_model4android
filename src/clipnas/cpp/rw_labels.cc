#include "rw_labels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json-c/json.h"
void free_yolo8_labelset(struct yolo8_labelset_s *labelset) {
    if(labelset->labels != NULL) {
        free(labelset->labels);
        labelset->labels = NULL;
    }
    // free(labelset);
}

void free_clip_labelset(struct clip_labelset_s *labelset) {
    if(labelset->filters != NULL) {
        for(int i = 0; i < labelset->filter_count; i++) {
            if(labelset->filters[i].categories != NULL) {
                free(labelset->filters[i].categories);
                labelset->filters[i].categories = NULL;
            }
        }
        free(labelset->filters);
        labelset->filters = NULL;
    }
    // free(labelset);
}

int read_yolo8_labels(struct yolo8_labelset_s *labelset, const char *file_path) {
    FILE *fp = NULL;
    char line[512];
    int ret = 0;
    int index = 0;
    char *token = NULL;
    struct yolo8_label_s *temp_labels = NULL;

    if (labelset == NULL || file_path == NULL) {
        return -1;
    }

    labelset->labels = NULL;
    labelset->label_count = 0;

    fp = fopen(file_path, "r");
    if (fp == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = 0;

        if (strlen(line) == 0) {
            continue;
        }

        temp_labels = (struct yolo8_label_s *)realloc(
            labelset->labels, 
            (labelset->label_count + 1) * sizeof(struct yolo8_label_s)
        );

        if (temp_labels == NULL) {
            ret = -1;
            goto cleanup;
        }

        labelset->labels = temp_labels;

        token = strtok(line, ",");
        if (token != NULL) {
            labelset->labels[labelset->label_count].class_id = atoi(token);
        }

        token = strtok(NULL, ",");
        if (token != NULL) {
            strncpy(
                labelset->labels[labelset->label_count].label_en, 
                token, 
                sizeof(labelset->labels[labelset->label_count].label_en) - 1
            );
            labelset->labels[labelset->label_count].label_en[sizeof(labelset->labels[labelset->label_count].label_en) - 1] = '\0';
        }

        token = strtok(NULL, ",");
        if (token != NULL) {
            strncpy(
                labelset->labels[labelset->label_count].label_cn, 
                token, 
                sizeof(labelset->labels[labelset->label_count].label_cn) - 1
            );
            labelset->labels[labelset->label_count].label_cn[sizeof(labelset->labels[labelset->label_count].label_cn) - 1] = '\0';
        }

        labelset->label_count++;
    }

cleanup:
    if (fp != NULL) {
        fclose(fp);
    }

    if (ret != 0) {
        if (labelset->labels != NULL) {
            free(labelset->labels);
            labelset->labels = NULL;
        }
        labelset->label_count = 0;
    }

    return ret;
}
int read_clip_labels(struct clip_labelset_s *labelset, const char *file_path) {
    FILE *fp = NULL;
    char *json_str = NULL;
    long file_size;
    struct json_object *parsed_json = NULL;
    struct json_object *filters_obj = NULL;
    struct json_object *filter_obj = NULL;
    struct json_object *categories_obj = NULL;
    struct json_object *category_obj = NULL;
    struct json_object *label_obj = NULL;
    struct json_object *classId_obj = NULL;
    struct json_object *labelName_obj = NULL;
    struct json_object *cn_obj = NULL;
    struct json_object *en_obj = NULL;
    struct json_object *clipText_obj = NULL;
    struct json_object *useFace_obj = NULL;
    struct json_object *useYolo_obj = NULL;
    struct json_object *useScene_obj = NULL;
    struct json_object *threshold_obj = NULL;
    int ret = 0;
    int i, j;
    int filter_count = 0;
    filter *temp_filters = NULL;
    category *temp_categories = NULL;

    if (labelset == NULL || file_path == NULL) {
        return -1;
    }

    labelset->filters = NULL;
    labelset->filter_count = 0;

    fp = fopen(file_path, "r");
    if (fp == NULL) {
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    json_str = (char *)malloc(file_size + 1);
    if (json_str == NULL) {
        ret = -1;
        goto cleanup;
    }

    if (fread(json_str, 1, file_size, fp) != (size_t)file_size) {
        ret = -1;
        goto cleanup;
    }
    json_str[file_size] = '\0';

    parsed_json = json_tokener_parse(json_str);
    if (parsed_json == NULL) {
        ret = -1;
        goto cleanup;
    }

    if (!json_object_object_get_ex(parsed_json, "filters", &filters_obj)) {
        ret = -1;
        goto cleanup;
    }

    filter_count = json_object_array_length(filters_obj);
    if (filter_count <= 0) {
        ret = -1;
        goto cleanup;
    }

    labelset->filters = (filter *)calloc(filter_count, sizeof(filter));
    if (labelset->filters == NULL) {
        ret = -1;
        goto cleanup;
    }
    labelset->filter_count = filter_count;

    for (i = 0; i < filter_count; i++) {
        filter_obj = json_object_array_get_idx(filters_obj, i);
        
        struct json_object *name_obj = NULL;
        struct json_object *type_obj = NULL;
        
        if (json_object_object_get_ex(filter_obj, "name", &name_obj)) {
            strncpy(labelset->filters[i].name, 
                    json_object_get_string(name_obj), 
                    sizeof(labelset->filters[i].name) - 1);
        }
        
        if (json_object_object_get_ex(filter_obj, "type", &type_obj)) {
            strncpy(labelset->filters[i].type, 
                    json_object_get_string(type_obj), 
                    sizeof(labelset->filters[i].type) - 1);
        }
        
        if (json_object_object_get_ex(filter_obj, "threshold", &threshold_obj)) {
            labelset->filters[i].threshold = (float)json_object_get_double(threshold_obj);
        }
        
        labelset->filters[i].categories = NULL;
        labelset->filters[i].category_count = 0;
        
        if (json_object_object_get_ex(filter_obj, "categories", &categories_obj)) {
            int category_count = json_object_array_length(categories_obj);
            
            if (category_count > 0) {
                labelset->filters[i].categories = (category *)calloc(
                    category_count, sizeof(category)
                );
                
                if (labelset->filters[i].categories == NULL) {
                    ret = -1;
                    goto cleanup;
                }
                
                labelset->filters[i].category_count = category_count;
                
                for (j = 0; j < category_count; j++) {
                    category_obj = json_object_array_get_idx(categories_obj, j);
                    
                    labelset->filters[i].categories[j].has_label = 0;
                    labelset->filters[i].categories[j].useFace = 0;
                    labelset->filters[i].categories[j].useYolo = 0;
                    labelset->filters[i].categories[j].useScene = 0;
                    labelset->filters[i].categories[j].clipText[0] = '\0';
                    
                    if (json_object_object_get_ex(category_obj, "useFace", &useFace_obj)) {
                        labelset->filters[i].categories[j].useFace = 
                            json_object_get_boolean(useFace_obj);
                    }
                    
                    if (json_object_object_get_ex(category_obj, "useYolo", &useYolo_obj)) {
                        labelset->filters[i].categories[j].useYolo = 
                            json_object_get_boolean(useYolo_obj);
                    }
                    
                    if (json_object_object_get_ex(category_obj, "useScene", &useScene_obj)) {
                        labelset->filters[i].categories[j].useScene = 
                            json_object_get_boolean(useScene_obj);
                    }
                    
                    if (json_object_object_get_ex(category_obj, "clipText", &clipText_obj)) {
                        strncpy(labelset->filters[i].categories[j].clipText,
                                json_object_get_string(clipText_obj),
                                sizeof(labelset->filters[i].categories[j].clipText) - 1);
                        labelset->filters[i].categories[j].clipText[
                            sizeof(labelset->filters[i].categories[j].clipText) - 1] = '\0';
                    }
                    
                    if (json_object_object_get_ex(category_obj, "label", &label_obj)) {
                        labelset->filters[i].categories[j].has_label = 1;
                        
                        if (json_object_object_get_ex(label_obj, "classId", &classId_obj)) {
                            labelset->filters[i].categories[j].label.classId = 
                                json_object_get_int(classId_obj);
                        }
                        
                        if (json_object_object_get_ex(label_obj, "labelName", &labelName_obj)) {
                            if (json_object_object_get_ex(labelName_obj, "cn", &cn_obj)) {
                                strncpy(labelset->filters[i].categories[j].label.labelName.cn,
                                        json_object_get_string(cn_obj),
                                        sizeof(labelset->filters[i].categories[j].label.labelName.cn) - 1);
                                labelset->filters[i].categories[j].label.labelName.cn[
                                    sizeof(labelset->filters[i].categories[j].label.labelName.cn) - 1] = '\0';
                            }
                            
                            if (json_object_object_get_ex(labelName_obj, "en", &en_obj)) {
                                strncpy(labelset->filters[i].categories[j].label.labelName.en,
                                        json_object_get_string(en_obj),
                                        sizeof(labelset->filters[i].categories[j].label.labelName.en) - 1);
                                labelset->filters[i].categories[j].label.labelName.en[
                                    sizeof(labelset->filters[i].categories[j].label.labelName.en) - 1] = '\0';
                            }
                        }
                    }
                }
            }
        }
    }

cleanup:
    if (json_str != NULL) {
        free(json_str);
    }
    
    if (parsed_json != NULL) {
        json_object_put(parsed_json);
    }
    
    if (fp != NULL) {
        fclose(fp);
    }
    
    if (ret != 0) {
        if (labelset->filters != NULL) {
            for (i = 0; i < labelset->filter_count; i++) {
                if (labelset->filters[i].categories != NULL) {
                    free(labelset->filters[i].categories);
                    labelset->filters[i].categories = NULL;
                }
            }
            free(labelset->filters);
            labelset->filters = NULL;
        }
        labelset->filter_count = 0;
    }

    return ret;
}