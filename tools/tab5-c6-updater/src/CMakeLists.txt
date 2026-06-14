# NOTE: the C6 image is a generated C array (c6fw_data.c) — both CMake
# EMBED_FILES and board_build.embed_files are broken under PlatformIO's
# espidf integration (the generated .S never reaches the link).
idf_component_register(SRCS "main.c" "c6fw_data.c"
                       INCLUDE_DIRS ".")
