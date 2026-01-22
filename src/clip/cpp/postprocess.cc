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
#include <math.h>
#include <float.h>
#include <vector>
#include <algorithm>
#include <arm_neon.h>

#include "clip.h"
#include "postprocess.h"

// ============ 内部辅助函数 ============

typedef struct {
    float value;
    int index;
} element_t;

// 比较函数用于 std::sort (降序)
static bool compare_elements(const element_t& a, const element_t& b) {
    return a.value > b.value;
}

static void softmax(float* arr, int size)
{
    // Find the maximum value in the array
    float max_val = -FLT_MAX;
    for (int i = 0; i < size; i++) {
        if (arr[i] > max_val) {
            max_val = arr[i];
        }
    }

    // Subtract the maximum value from each element to avoid overflow
    // and compute sum of exponentials
    float sum_exp = 0.0f;
    for (int i = 0; i < size; i++) {
        arr[i] = expf(arr[i] - max_val);
        sum_exp += arr[i];
    }

    // Normalize
    for (int i = 0; i < size; i++) {
        arr[i] /= sum_exp;
    }
}

static void matmul_by_cpu(const float* A, const float* B, float* out, int M, int K, int N)
{
    // A: [M, K]    B: [N, K] (转置存储的文本特征通常是 [N, K] 如果是行主序，但这里 B 是 text_output [text_num, text_dim])
    // 假设 A 是 image [1, dim], B 是 text [num, dim]
    // 我们计算 A * B^T -> [1, num]
    
    // 注意：原本的实现中 loop 顺序和参数命名有点混淆，这里修正逻辑
    // A_rows = M, A_cols = K
    // B_rows = N, B_cols = K (因为要把 B 当作 B^T 使用，通常文本特征是每一行一个文本向量)
    
    for (int i = 0; i < M; i++)
    {
        for (int j = 0; j < N; j++)
        {
            float sum = 0.0f;
            for (int k = 0; k < K; k++)
            {
                sum += A[i * K + k] * B[j * K + k];
            }
            out[i * N + j] = sum;
        }
    }
}

// ============ 向量运算实现 ============

float vector_l2_norm(const float* vec, int dim) {
    float sum = 0.0f;
    for (int i = 0; i < dim; ++i) {
        sum += vec[i] * vec[i];
    }
    return sqrtf(sum);
}

void vector_l2_normalize(float* vec, int dim) {
    float norm = vector_l2_norm(vec, dim);
    if (norm > 1e-6f) {
        float inv_norm = 1.0f / norm;
        for (int i = 0; i < dim; ++i) {
            vec[i] *= inv_norm;
        }
    }
}

float vector_cosine_similarity(const float* vec1, const float* vec2, int dim) {
    // 假设 vec1 和 vec2 已经归一化
    float dot = 0.0f;
    for (int i = 0; i < dim; ++i) {
        dot += vec1[i] * vec2[i];
    }
    return dot;
}

void normalize_text_features(float* text_output, int text_num, int text_dim) {
    for (int i = 0; i < text_num; ++i) {
        vector_l2_normalize(text_output + i * text_dim, text_dim);
    }
}

// ============ 匹配逻辑实现 ============

int clip_match(float* img_output, int img_dim,
               float* text_output, int text_num, int text_dim,
               clip_res* out_res)
{
    // 复用 clip_match_topk 取 Top-1
    clip_topk_res topk_res;
    int ret = clip_match_topk(img_output, img_dim, text_output, text_num, text_dim, 1, 0.0f, &topk_res);
    if (ret == 0 && topk_res.count > 0) {
        *out_res = topk_res.results[0];
    } else {
        return -1;
    }
    return 0;
}

int clip_match_topk(float* img_output, int img_dim,
                    float* text_output, int text_num, int text_dim,
                    int topk, float threshold,
                    clip_topk_res* out_res)
{
    if (!img_output || !text_output || !out_res) return -1;
    if (img_dim != text_dim) {
        printf("Dimension mismatch: img %d != text %d\n", img_dim, text_dim);
        return -1;
    }

    // 1. 直接计算 Dot Product (移除 L2 归一化以恢复精度)
    // 注意：用户反馈归一化会导致精度下降，因此回退到原始的点积逻辑
    
    std::vector<float> logits(text_num);
    // 这里不再分配大块 matmul_out，直接复用 vector
    
    // 手动实现点积，避免分配大矩阵
    for (int i = 0; i < text_num; ++i) {
        float dot = 0.0f;
        const float* txt_vec = text_output + i * text_dim;
        
        // 简单的循环展开或利用 NEON 可以加速这里
        // 为了简单起见，这里保持标准 C++
        for (int k = 0; k < img_dim; ++k) {
            dot += img_output[k] * txt_vec[k];
        }
        // Bug Fix: 原始代码中乘以的是 expf(logit_scale) ≈ 100.0
        // 而不是 scale 本身 (4.605)。这导致了严重的精度问题。
        logits[i] = dot * expf(CLIP_DEFAULT_LOGIT_SCALE);
    }

    // 3. Softmax
    std::vector<float> iconf = logits; // 复制一份用于 softmax
    softmax(iconf.data(), text_num);

    // 4. 收集结果并排序
    std::vector<element_t> candidates;
    candidates.reserve(text_num);
    for (int i = 0; i < text_num; ++i) {
        if (iconf[i] >= threshold) {
            candidates.push_back({iconf[i], i});
        }
    }

    // 如果所有结果都低于阈值，至少返回一个（如果阈值不是0）
    if (candidates.empty() && text_num > 0) {
        // 找最大的一个
         float max_val = -1.0f;
         int max_idx = 0;
         for(int i=0; i<text_num; ++i) {
             if(iconf[i] > max_val) {
                 max_val = iconf[i];
                 max_idx = i;
             }
         }
         candidates.push_back({max_val, max_idx});
    }

    std::sort(candidates.begin(), candidates.end(), compare_elements);

    // 5. 填充输出
    out_res->count = 0;
    int limit = (topk < candidates.size()) ? topk : candidates.size();
    if (limit > CLIP_MAX_TOPK) limit = CLIP_MAX_TOPK;

    for (int i = 0; i < limit; ++i) {
        out_res->results[i].img_index = 0; // 单图模式
        out_res->results[i].text_index = candidates[i].index;
        out_res->results[i].score = candidates[i].value;
        out_res->results[i].logit = logits[candidates[i].index]; // 原始 Logit
        out_res->count++;
    }

    return 0;
}

// ============ 旧 API 兼容层 ============

int post_process(rknn_app_context_t* app_ctx, float* img_output, float* text_output, clip_res* out_res)
{
    // 注意：旧逻辑中可能没有预先归一化文本特征
    // 但为了保持一致性，我们在这里做临时归一化并不太高效（因为这是每帧调用的）
    // 鉴于这是一个演示代码，我们假设用户如果用旧 API，可能不在乎那么多优化
    // 或者我们直接调用 clip_match，并在其中做归一化
    
    int img_dim = app_ctx->img.output_attrs[0].dims[1];
    int text_dim = app_ctx->text.output_attrs[0].dims[1];
    int text_num = app_ctx->input_text_num;
    
    // 移除归一化，恢复点积逻辑
    // normalize_text_features(text_output, text_num, text_dim);
    
    return clip_match(img_output, img_dim, text_output, text_num, text_dim, out_res);
}

// ============ 特征持久化 ============

#define CLIP_FEAT_MAGIC "CLIP_TEXT_FEAT"
#define CLIP_FEAT_MAGIC_LEN 14

typedef struct {
    char magic[CLIP_FEAT_MAGIC_LEN];
    uint32_t version;
    int32_t text_num;
    int32_t feature_dim;
    char reserved[16];
} clip_feat_header_t;

int save_text_features(const char* filepath, 
                       float* text_features, 
                       int text_num, 
                       int feature_dim)
{
    FILE* fp = fopen(filepath, "wb");
    if (!fp) {
        printf("Failed to open file for writing: %s\n", filepath);
        return -1;
    }

    clip_feat_header_t header;
    memset(&header, 0, sizeof(header));
    strncpy(header.magic, CLIP_FEAT_MAGIC, CLIP_FEAT_MAGIC_LEN);
    header.version = 1;
    header.text_num = text_num;
    header.feature_dim = feature_dim;

    fwrite(&header, sizeof(header), 1, fp);
    fwrite(text_features, sizeof(float), text_num * feature_dim, fp);

    fclose(fp);
    return 0;
}

int load_text_features(const char* filepath,
                       float** text_features_ptr,
                       int* text_num,
                       int* feature_dim)
{
    FILE* fp = fopen(filepath, "rb");
    if (!fp) return -1;

    clip_feat_header_t header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    if (strncmp(header.magic, CLIP_FEAT_MAGIC, CLIP_FEAT_MAGIC_LEN) != 0) {
        printf("Invalid magic header\n");
        fclose(fp);
        return -1;
    }

    *text_num = header.text_num;
    *feature_dim = header.feature_dim;

    size_t data_size = (size_t)header.text_num * header.feature_dim * sizeof(float);
    float* features = (float*)malloc(data_size);
    if (!features) {
        fclose(fp);
        return -1;
    }

    if (fread(features, 1, data_size, fp) != data_size) {
        printf("Failed to read feature data\n");
        free(features);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *text_features_ptr = features;
    return 0;
}