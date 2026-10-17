idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES yucca_core yucca_app yucca_console input_bluepad32 target_wii btstack esp_driver_gpio esp_timer esp_system
)
