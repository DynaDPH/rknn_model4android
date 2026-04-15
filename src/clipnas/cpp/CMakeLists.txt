cmake_minimum_required(VERSION 3.10)

project(clipmaster)

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/ utils.out)

add_definitions(-DROOT_PATH="${ROOT_PATH}")

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# CLIP 相关文件
set(clip_file rknpu2/clip.cc)
set(rknn_clip_utils rknpu2/rknn_clip_utils/rknn_clip_utils.cc)
set(clip_tokenizer tokenizer/clip_tokenizer.cpp)

# YOLOv8 相关文件
set(yolov8_file rknpu2/yolov8.cc)

add_executable(${PROJECT_NAME}
    main.cc
	postprocess.cc
    postprocess_yolo8.cc
    rw_labels.cc
    ${clip_file}
    ${rknn_clip_utils}
    ${clip_tokenizer}
    ${yolov8_file}
)

target_link_libraries(${PROJECT_NAME}
	imageutils
    fileutils
    busuds
    dma_alloc
    ${LIBRKNNRT}
    dl
)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_link_libraries(${PROJECT_NAME}
        log
        ${LIBJSONC}
    )
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(${PROJECT_NAME}
        pthread
        ${LIBJSONC}
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tokenizer
    ${CMAKE_CURRENT_SOURCE_DIR}/rknpu2/rknn_clip_utils
    ${LIBRKNNRT_INCLUDES}
    ${LIBJSONCFILE_INCLUDES}
    ${DMA_ALLOC_INCLUDES}
)

# 安装可执行文件
install(TARGETS ${PROJECT_NAME} DESTINATION .)

# 安装模型文件
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
if(RKNN_FILES)
    install(FILES ${RKNN_FILES} DESTINATION model)
endif()

# 安装标签文件
file(GLOB LABEL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.json" "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.csv")
if(LABEL_FILES)
    install(FILES ${LABEL_FILES} DESTINATION model)
endif()