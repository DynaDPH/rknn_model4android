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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clip.h"
#include "common.h"
#include "file_utils.h"
#include "image_utils.h"


int init_clip_model(const char* img_model_path, const char* text_model_path, rknn_app_context_t* app_ctx)
{
    int ret;

    printf("--> init clip image model\n");
    ret = init_clip_model_utils(&(app_ctx->img), img_model_path);
    if (ret < 0)
    {
        printf("rknn_init clip image model fail! ret=%d\n", ret);
        return -1;
    }

    printf("--> init clip text model\n");
    ret = init_clip_model_utils(&(app_ctx->text), text_model_path);
    if (ret < 0)
    {
        printf("rknn_init clip text model fail! ret=%d\n", ret);
        return -1;
    }

    app_ctx->clip_tokenize = new CLIPTokenizer();

    return 0;
}

int release_clip_model(rknn_app_context_t* app_ctx)
{
    release_clip_model_utils(&(app_ctx->img));
    release_clip_model_utils(&(app_ctx->text));
    delete app_ctx->clip_tokenize;

    return 0;

}
int inference_clip_model(rknn_app_context_t* app_ctx, image_buffer_t* img, char** input_texts, int text_num, clip_res* out_res)
{
    int ret;
    int* tokens = NULL;
    float* img_output = NULL;
    float* text_output = NULL;

    if ((!app_ctx) || (!img))
    {
        printf("app_ctx or img is NULL");
        return -1;
    }

    if (text_num > MAX_TEXT_NUM)
    {
        text_num = MAX_TEXT_NUM;
        printf("Input text num overlimit, modify text num == %d", MAX_TEXT_NUM);
    }
    int sequence_len = app_ctx->text.input_attrs[0].dims[1];
    int tokens_num = text_num * sequence_len;
    
    // Allocate memory
    tokens = (int*)malloc(tokens_num * sizeof(int));
    
    int img_out_size = app_ctx->img.output_attrs[0].dims[0] * app_ctx->img.output_attrs[0].dims[1];
    img_output = (float*)malloc(img_out_size * sizeof(float));
    
    int text_dim = app_ctx->text.output_attrs[0].dims[1];
    int text_out_size = text_num * text_dim;
    text_output = (float*)malloc(text_out_size * sizeof(float));

    if (!tokens || !img_output || !text_output) {
        printf("Failed to allocate memory for inference buffers\n");
        ret = -1;
        goto out;
    }

    if (app_ctx->img.output_attrs[0].dims[1] != app_ctx->text.output_attrs[0].dims[1])
    {   
        printf("The dimensions of the img and text model output are not the same! Please confirm that are consistent");
        ret = -1;
        goto out;
    }
    memset(img_output, 0, img_out_size * sizeof(float));
    memset(text_output, 0, text_out_size * sizeof(float));

    app_ctx->input_img_num = 1;
    app_ctx->input_text_num = text_num;

    for (int i = 0; i < text_num; i++)
    {
        std::vector<int> token = app_ctx->clip_tokenize->tokenize(input_texts[i], sequence_len, true);
        for (int j = 0; j < token.size(); j++)
        {
            tokens[i*sequence_len+j] = token[j];
        }
    }

    printf("--> inference clip image model\n");
    ret = inference_clip_image_model_utils(&(app_ctx->img), img, img_output);
    if (ret != 0)
    {
        printf("inference clip image model fail! ret=%d\n", ret);
        goto out;
    }

    printf("--> inference clip text model\n");
    for (int i = 0; i < text_num; i++)
    {   
        ret = inference_clip_text_model_utils(&(app_ctx->text), tokens + (i*sequence_len), text_output + (i*text_dim));
        if (ret != 0)
        {
            printf("inference clip text model fail! ret=%d\n", ret);
            goto out;
        }
    }

    // Post Process
    post_process(app_ctx, img_output, text_output, out_res);

out:
    if (tokens != NULL) free(tokens);
    if (img_output != NULL) free(img_output);
    if (text_output != NULL) free(text_output);

    return ret;
}

// ============ 新增解耦API实现 ============

int inference_clip_text_only(rknn_app_context_t* app_ctx,
                              char** input_texts,
                              int text_num,
                              float** text_output_ptr,
                              int* feature_dim)
{
    int ret = 0;
    int* tokens = NULL;
    float* text_output = NULL;

    if (app_ctx == NULL || input_texts == NULL || text_output_ptr == NULL || feature_dim == NULL)
    {
        printf("inference_clip_text_only: invalid parameters\n");
        return -1;
    }

    if (text_num <= 0)
    {
        printf("inference_clip_text_only: text_num must be positive\n");
        return -1;
    }

    int feature_dim_value = app_ctx->text.output_attrs[0].dims[1];
    *feature_dim = feature_dim_value;

    int sequence_len = app_ctx->text.input_attrs[0].dims[1];
    int tokens_num = text_num * sequence_len;

    // 堆分配 tokens
    tokens = (int*)malloc(tokens_num * sizeof(int));
    if (tokens == NULL)
    {
        printf("inference_clip_text_only: failed to allocate memory for tokens\n");
        return -1;
    }

    // 堆分配文本特征数组
    text_output = (float*)malloc(text_num * feature_dim_value * sizeof(float));
    if (text_output == NULL)
    {
        printf("inference_clip_text_only: failed to allocate memory for text_output\n");
        free(tokens);
        return -1;
    }
    memset(text_output, 0, text_num * feature_dim_value * sizeof(float));

    // Tokenize 所有文本
    for (int i = 0; i < text_num; i++)
    {
        std::vector<int> token = app_ctx->clip_tokenize->tokenize(input_texts[i], sequence_len, true);
        for (size_t j = 0; j < token.size(); j++)
        {
            tokens[i * sequence_len + j] = token[j];
        }
    }

    // 推理所有文本
    printf("--> inference clip text model (%d texts)\n", text_num);
    for (int i = 0; i < text_num; i++)
    {
        ret = inference_clip_text_model_utils(&(app_ctx->text),
                                               tokens + (i * sequence_len),
                                               text_output + (i * feature_dim_value));
        if (ret != 0)
        {
            printf("inference clip text model fail! ret=%d text_index=%d\n", ret, i);
            free(tokens);
            free(text_output);
            return -1;
        }
    }

    free(tokens);
    *text_output_ptr = text_output;

    return 0;
}

int inference_clip_image_only(rknn_app_context_t* app_ctx,
                               image_buffer_t* img,
                               float** img_output_ptr,
                               int* feature_dim)
{
    int ret = 0;
    float* img_output = NULL;

    if (app_ctx == NULL || img == NULL || img_output_ptr == NULL || feature_dim == NULL)
    {
        printf("inference_clip_image_only: invalid parameters\n");
        return -1;
    }

    int feature_dim_value = app_ctx->img.output_attrs[0].dims[1];
    *feature_dim = feature_dim_value;

    // 堆分配图片特征数组
    img_output = (float*)malloc(feature_dim_value * sizeof(float));
    if (img_output == NULL)
    {
        printf("inference_clip_image_only: failed to allocate memory for img_output\n");
        return -1;
    }
    memset(img_output, 0, feature_dim_value * sizeof(float));

    printf("--> inference clip image model\n");
    ret = inference_clip_image_model_utils(&(app_ctx->img), img, img_output);
    if (ret != 0)
    {
        printf("inference clip image model fail! ret=%d\n", ret);
        free(img_output);
        return -1;
    }

    *img_output_ptr = img_output;

    return 0;
}

void free_clip_features(float* features)
{
    if (features != NULL)
    {
        free(features);
    }
}