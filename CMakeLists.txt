cmake_minimum_required(VERSION 3.16)
# 指定ADF
include($ENV{ADF_PATH}/CMakeLists.txt)
# 指定IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 定义项目
project(ESP32-S3-BOX)