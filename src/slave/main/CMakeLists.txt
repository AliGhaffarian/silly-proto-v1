idf_component_register(SRCS main.c slave.c
                        REQUIRES nvs_flash esp_driver_uart silly_proto shared_uart_setup uart_utils
                        INCLUDE_DIRS "")
nvs_create_partition_image("nvs" "../silly_params.csv" FLASH_IN_PROJECT)
