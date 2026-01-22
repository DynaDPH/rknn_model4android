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

#ifndef _RKNN_DEMO_CLIP_POSTPROCESS_H_
#define _RKNN_DEMO_CLIP_POSTPROCESS_H_

// ============ 配置常量 ============
#define CLIP_DEFAULT_LOGIT_SCALE 4.605170249938965f  // ln(100) ≈ 4.605
#define CLIP_DEFAULT_THRESHOLD 0.0f                   // 默认无阈值过滤
#define CLIP_MAX_TOPK 10                              // 最大支持的 Top-K

// ============ 结果结构体 ============

/**
 * @brief 单个匹配结果
 */
typedef struct {
    int img_index;
    int text_index;
    float score;       // softmax 后的概率
    float logit;       // softmax 前的 logit 值
} clip_res;

/**
 * @brief Top-K 匹配结果
 */
typedef struct {
    int count;                      // 实际返回的结果数量
    clip_res results[CLIP_MAX_TOPK]; // 按得分降序排列的结果
} clip_topk_res;

// ============ 向量运算函数 ============

/**
 * @brief 计算向量的 L2 范数
 * @param vec 输入向量
 * @param dim 向量维度
 * @return L2 范数
 */
float vector_l2_norm(const float* vec, int dim);

/**
 * @brief 原地 L2 归一化向量
 * @param vec 输入/输出向量
 * @param dim 向量维度
 * @note CLIP 特征必须先 L2 归一化才能正确计算余弦相似度
 */
void vector_l2_normalize(float* vec, int dim);

/**
 * @brief 计算两个 L2 归一化向量的余弦相似度（点积）
 * @param vec1 第一个向量（已归一化）
 * @param vec2 第二个向量（已归一化）
 * @param dim 向量维度
 * @return 余弦相似度 [-1, 1]
 */
float vector_cosine_similarity(const float* vec1, const float* vec2, int dim);

// ============ 原有 API（向后兼容）============

/**
 * @brief 原有后处理函数（依赖 app_ctx）
 */
int post_process(rknn_app_context_t* app_ctx, float* img_output, float* text_output, clip_res* out_res);

// ============ 新增解耦 API ============

/**
 * @brief 解耦的特征匹配函数，不依赖 app_ctx
 * @param img_output 图片特征向量
 * @param img_dim 图片特征维度
 * @param text_output 文本特征数组（text_num * text_dim）
 * @param text_num 文本数量
 * @param text_dim 每个文本的特征维度
 * @param out_res [输出] 匹配结果
 * @return 0 成功，-1 失败
 */
int clip_match(float* img_output, int img_dim,
               float* text_output, int text_num, int text_dim,
               clip_res* out_res);

/**
 * @brief 增强版特征匹配函数，支持 Top-K 和阈值过滤
 * @param img_output 图片特征向量（将被原地 L2 归一化）
 * @param img_dim 图片特征维度
 * @param text_output 文本特征数组（应已 L2 归一化）
 * @param text_num 文本数量
 * @param text_dim 每个文本的特征维度
 * @param topk 返回的最大结果数量（1 ~ CLIP_MAX_TOPK）
 * @param threshold 置信度阈值（低于此值的结果将被过滤，0 表示不过滤）
 * @param out_res [输出] Top-K 匹配结果
 * @return 0 成功，-1 失败
 */
int clip_match_topk(float* img_output, int img_dim,
                    float* text_output, int text_num, int text_dim,
                    int topk, float threshold,
                    clip_topk_res* out_res);

/**
 * @brief 批量归一化文本特征（在缓存时调用一次）
 * @param text_output 文本特征数组（将被原地归一化）
 * @param text_num 文本数量
 * @param text_dim 每个文本的特征维度
 */
void normalize_text_features(float* text_output, int text_num, int text_dim);

// ============ 特征持久化 API ============

/**
 * @brief 保存文本特征到文件
 * @param filepath 文件路径
 * @param text_features 文本特征数组
 * @param text_num 文本数量
 * @param feature_dim 特征维度
 * @return 0 成功，-1 失败
 */
int save_text_features(const char* filepath, 
                       float* text_features, 
                       int text_num, 
                       int feature_dim);

/**
 * @brief 从文件加载文本特征
 * @param filepath 文件路径
 * @param text_features_ptr [输出] 堆分配的文本特征数组
 * @param text_num [输出] 文本数量
 * @param feature_dim [输出] 特征维度
 * @return 0 成功，-1 失败
 */
int load_text_features(const char* filepath,
                       float** text_features_ptr,
                       int* text_num,
                       int* feature_dim);

#endif // _RKNN_DEMO_CLIP_POSTPROCESS_H_