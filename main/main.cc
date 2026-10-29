#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "hud/hud_integration.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
#if CONFIG_WAZE_HUD_AI
    // HUD/BLE is optional relative to the base device.  Keep the display and
    // recovery UI alive if a future configuration or memory regression makes
    // the HUD unavailable instead of aborting and rebooting forever.
    ret = waze_hud::StartHud();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WazeHUD start failed: %s; continuing without HUD", esp_err_to_name(ret));
    }
#endif
    // Wi-Fi allocates several internal-SRAM pools.  It must start only after
    // the BLE controller has reserved its contiguous memory.
    app.StartNetwork();
    app.Run();  // This function runs the main event loop and never returns
}
