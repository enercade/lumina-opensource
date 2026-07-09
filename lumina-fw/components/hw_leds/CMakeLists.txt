idf_component_register(
    SRCS "hw_leds.cpp"
    INCLUDE_DIRS "include"
    REQUIRES 
        "freertos"
        "main"
    PRIV_REQUIRES 
        "driver"
)