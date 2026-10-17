idf_component_register(
    SRCS "src/wii_target.cpp"
    INCLUDE_DIRS "include"
    REQUIRES yucca_core
    PRIV_REQUIRES wiimote_anything btstack
)
