idf_component_register(
    SRCS "src/console.cpp"
    INCLUDE_DIRS "include"
    REQUIRES yucca_core yucca_app
    PRIV_REQUIRES console esp_app_format freertos
)
