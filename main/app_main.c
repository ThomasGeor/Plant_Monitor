/*
 * Brief: Plant monitor system initialization.
 * author: Thomas Georgiadis
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "adc.h"
#include "tc74.h"
#include "MQ135.h"
#include "dht22.h"
#include "ldr.h"
#include "sen0193.h"
#include "wifi.h"
#include "file_system.h"
#include "sensor_data.h"

static const char *tag = "Debug";
TaskHandle_t init_task_handle = NULL;
TaskHandle_t update_task_handle = NULL;

void init(void)
{
  adc_init();
  tc74_init();
  mq135_init();
  ldr_init();
  sen0193_init();
  init_file_system();
  wifi_init(xTaskGetCurrentTaskHandle());

  // Wait for a direct task notification (block indefinitely)
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  xTaskNotifyGive(update_task_handle);
  // Delete the init task after full initialization is done.
  vTaskDelete(NULL);
}

void update(void)
{
    const TickType_t delay_ticks = pdMS_TO_TICKS(10000);
    // Wait for init task to finish
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while(1)
    {
      status_update();
      vTaskDelay(delay_ticks);
    }
}

void app_main(void)
{
  esp_log_level_set(tag, ESP_LOG_ERROR);
  xTaskCreate(init, "init_task", 8192, NULL, 6, &init_task_handle);
  xTaskCreate(update, "update_task", 4096, NULL, 5, &update_task_handle);
}
