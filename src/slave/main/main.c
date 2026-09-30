#include "nvs.h"
#include "nvs_flash.h"
#include "shared_uart_setup.h"
#include "slave.h"

#define PKT_TIMEOUT 530

void app_main(void)
{
    shared_setup_uart(NULL, 0);
    nvs_handle_t silly_params_handle = 0;
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READONLY, &silly_params_handle));
    silly_slave_id_t my_slave_id;
    nvs_get_u8(silly_params_handle, "my_slave_id", &my_slave_id);

    struct slave_ctx ctx = {
        .uart_port = SHARED_UART_NUM,
        .synced_until_id = 0,
        .last_pkt_id = 0,
        .slave_do_state_timeout = PKT_TIMEOUT,
        .state = SLAVE_STATES_IDLE,
        .my_slave_id = my_slave_id,
        .master_synced_until_id = 0};

    // get the token
    while(1) {
        ESP_LOGI("slave:main", "current state:%d", ctx.state);
        __builtin_dump_struct(&ctx, printf);
        state_handlers[ctx.state](&ctx, PKT_TIMEOUT);
    }
}
