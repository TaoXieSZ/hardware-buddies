# StackChan Voice Agent -- ESP-IDF Root CMakeLists
# NOTE: do NOT set() EXTRA_COMPONENT_DIRS here -- PlatformIO passes the src/
# main-component path through that cache variable and a plain set() clobbers
# it ("Couldn't find the main target"). components/ is auto-scanned by IDF.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stackchan-voice-agent)
