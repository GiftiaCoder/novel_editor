cmake_minimum_required(VERSION 3.16)
project(novel_editor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(Threads REQUIRED)
find_package(gflags REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(novel_editor
    main.cpp
    config.cpp
    utils.cpp
    auth.cpp
    storage.cpp
    docx_export.cpp
    command_hooks.cpp
    routes.cpp
)

if(DEFINED ENV{CONDA_PREFIX})
    message(STATUS "Using Conda environment: $ENV{CONDA_PREFIX}")
    target_include_directories(novel_editor PRIVATE "$ENV{CONDA_PREFIX}/include")
    target_link_directories(novel_editor PRIVATE "$ENV{CONDA_PREFIX}/lib")
else()
    message(WARNING "CONDA_PREFIX is not set")
endif()

target_link_libraries(novel_editor PRIVATE
    gflags
    Threads::Threads
    nlohmann_json::nlohmann_json
)

target_include_directories(novel_editor PRIVATE ${CMAKE_SOURCE_DIR}/src)
