cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3" CACHE STRING "ESP-IDF target")

# A reused module derives include paths directly from IDF_PATH. Normalize
# Windows backslashes before component discovery so CMake does not parse "\U"
# in C:\Users as an escape sequence.
file(TO_CMAKE_PATH "$ENV{IDF_PATH}" CODEX_CARDPUTER_IDF_PATH)
set(ENV{IDF_PATH} "${CODEX_CARDPUTER_IDF_PATH}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(codex_micro_cardputer)
