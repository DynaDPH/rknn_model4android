cmake_minimum_required(VERSION 3.10)
project(clip_master VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall")

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/../../utils
    ${CMAKE_SOURCE_DIR}/../cpp
    ${CMAKE_SOURCE_DIR}/../../3rdparty/rknpu2/include
    ${CMAKE_SOURCE_DIR}/../../3rdparty/json-c/include
)

# 链接库目录
link_directories(
    ${CMAKE_SOURCE_DIR}/../../install/rknpu2
    ${CMAKE_SOURCE_DIR}/../../3rdparty/json-c/lib
)

# 源文件
set(SOURCES
    clip_master.c
    ${CMAKE_SOURCE_DIR}/../cpp/postprocess.cc
    ${CMAKE_SOURCE_DIR}/../cpp/tokenizer/clip_tokenizer.cpp
)

# 可执行文件
add_executable(clip_master ${SOURCES})

# 链接库
target_link_libraries(clip_master
    rknn_api
    json-c
    m
    stdc++
)

# 安装目标
install(TARGETS clip_master DESTINATION bin)