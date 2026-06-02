cmake_minimum_required(VERSION 3.16)
# 指定ADF
include($ENV{ADF_PATH}/CMakeLists.txt)
# 指定IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 定义项目
project(ESP32-S3-BOX)

# 添加全局编译定义
add_compile_definitions(ES7210_ADDR=0x41)
