idf_component_register(
    SRCS "src/gamepad.cpp" "src/mapper.cpp" "src/settings.cpp" "src/settings_schema.cpp" "src/nvs_settings.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash
)
