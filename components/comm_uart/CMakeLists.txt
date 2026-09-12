# components/comm_uart/CMakeLists.txt
idf_component_register(
    SRCS "src/comm_uart.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver          # cho driver/uart.h
)