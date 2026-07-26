# Agora RTSA (IoT) SDK — prebuilt esp32s3 libs + IDF5 FreeRTOS compat shim.
idf_component_register(SRCS "freertos_compat.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES lwip pthread)

add_prebuilt_library(rtsa "${CMAKE_CURRENT_SOURCE_DIR}/libs/librtsa.a" PRIV_REQUIRES lwip pthread)
add_prebuilt_library(ahpl "${CMAKE_CURRENT_SOURCE_DIR}/libs/libahpl.a" PRIV_REQUIRES lwip pthread)
add_prebuilt_library(agora-cjson "${CMAKE_CURRENT_SOURCE_DIR}/libs/libagora-cjson.a")

target_link_libraries(${COMPONENT_LIB} PUBLIC rtsa ahpl agora-cjson)
