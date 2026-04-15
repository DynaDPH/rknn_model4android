cmake_minimum_required(VERSION 3.10)

project(mp_dispatch)

option(CLIP_CLIENT_DEMO "compile clip_client_demo" ON)

if (CLIP_CLIENT_DEMO)
    set(TEST_CLIP "clip_client_demo")
endif ()

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/ utils.out)
add_definitions(-DROOT_DIR="${ROOT_PATH}")
message(STATUS "${ROOT_PATH}")

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

add_executable(${PROJECT_NAME}
    main.cc
)

target_link_libraries(${PROJECT_NAME}
    busuds
)

if (CLIP_CLIENT_DEMO)
    add_executable(${TEST_CLIP}
        test_clip_client.cc
    )
    target_link_libraries(${TEST_CLIP}
        busuds
        ${LIBJSONC}
    )
    target_include_directories(${TEST_CLIP} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${LIBJSONCFILE_INCLUDES}
    )
    install(TARGETS ${TEST_CLIP} DESTINATION .)
endif ()


if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(${PROJECT_NAME}
        pthread
)
    target_link_libraries(clip_client_demo
        pthread
)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

install(TARGETS ${PROJECT_NAME} DESTINATION .)