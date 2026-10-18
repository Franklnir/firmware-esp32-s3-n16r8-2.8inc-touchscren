#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "board.h"

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

    // Initialize the application (this also initializes the board)
    auto& app = Application::GetInstance();
    app.Initialize();

    // Check if Chronchi mode was selected
    auto& board = Board::GetInstance();
    // Dynamic cast to check if it's our board with Chronchi support
    // We use a virtual method or check via the board type
    
    // For Chronchi mode, we need to bypass the normal app.Run()
    // and run the Chronchi loop instead
    // The mode was already selected in the board constructor
    
    // Run the normal application (Xiaozhi mode)
    // If Chronchi mode was selected, the board constructor already returned early
    // and the app will handle it differently
    app.Run();  // This function runs the main event loop and never returns
}
