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


#ifndef _RKNN_DEMO_CLIP_H_
#define _RKNN_DEMO_CLIP_H_

#include "rknn_api.h"
#include "common.h"
#include "clip_tokenizer.h"
#include "rknn_clip_utils.h"

// 保留原有限制用于向后兼容，但实际上新API无此限制
#define MAX_TEXT_NUM 1024

typedef struct {
    rknn_clip_context img;
    rknn_clip_context text;
    CLIPTokenizer* clip_tokenize;

    int input_img_num;
    int input_text_num;
} rknn_app_context_t;

#include "postprocess.h"

int init_clip_model(const char* img_model_path,
                    const char* text_model_path,
                    rknn_app_context_t* app_ctx);

int release_clip_model(rknn_app_context_t* app_ctx);

// 原有API - 保留向后兼容
int inference_clip_model(rknn_app_context_t* app_ctx,
                        image_buffer_t* img,
                        char** input_texts,
                        int text_num,
                        clip_res* out_res
                        );

// ============ 新增解耦API ============

/**
 * @brief 单独推理文本标签，返回堆分配的特征向量
 * @param app_ctx 应用上下文
 * @param input_texts 输入文本数组
 * @param text_num 文本数量（无上限限制）
 * @param text_output_ptr [输出] 堆分配的文本特征数组，调用者需使用 free_clip_features 释放
 * @param feature_dim [输出] 每个文本的特征维度
 * @return 0 成功，-1 失败
 */
int inference_clip_text_only(rknn_app_context_t* app_ctx,
                              char** input_texts,
                              int text_num,
                              float** text_output_ptr,
                              int* feature_dim);

/**
 * @brief 单独推理图片，返回堆分配的特征向量
 * @param app_ctx 应用上下文
 * @param img 输入图片
 * @param img_output_ptr [输出] 堆分配的图片特征数组，调用者需使用 free_clip_features 释放
 * @param feature_dim [输出] 图片的特征维度
 * @return 0 成功，-1 失败
 */
int inference_clip_image_only(rknn_app_context_t* app_ctx,
                               image_buffer_t* img,
                               float** img_output_ptr,
                               int* feature_dim);

/**
 * @brief 释放特征向量内存
 * @param features 由 inference_clip_text_only 或 inference_clip_image_only 返回的特征指针
 */
void free_clip_features(float* features);

#endif //_RKNN_DEMO_CLIP_H_