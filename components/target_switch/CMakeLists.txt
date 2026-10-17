idf_component_register(
    SRCS "src/switch_target.cpp"
    INCLUDE_DIRS "include"
    REQUIRES yucca_core bt_hid_device switch_pro btstack
)
