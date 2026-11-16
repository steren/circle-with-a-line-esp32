cmake_minimum_required(VERSION 3.22)

# ESP-IDF picks up components/amoled-board from the components/ folder by itself.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(circle-with-a-line)
