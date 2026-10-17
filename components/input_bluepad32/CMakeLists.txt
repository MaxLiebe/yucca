idf_component_register(
    SRCS "src/bluepad32_input.cpp" "src/convert.cpp"
    INCLUDE_DIRS "include"
    REQUIRES yucca_core
    PRIV_REQUIRES bluepad32 btstack esp_timer
)
