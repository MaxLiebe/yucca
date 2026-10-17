idf_component_register(
    SRCS "src/app.cpp"
    INCLUDE_DIRS "include"
    REQUIRES yucca_core
    PRIV_REQUIRES log
)
