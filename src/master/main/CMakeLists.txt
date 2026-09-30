idf_component_register(SRCS main.c master.c
                        REQUIRES esp_driver_uart silly_proto shared_uart_setup uart_utils
                        INCLUDE_DIRS "")
