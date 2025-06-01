/*
 * Brief: Wifi connection driver.
 *        MDNS initialization.
 * author : Thomas Georgiadis
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "mdns.h"
#include "lwip/apps/netbiosns.h"

#include "http_server.h"

#define WIFI_SSID
#define WIFI_PASS
#define MAXIMUM_RETRY 5

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_TIMEOUT 300

#define MDNS_HOST_NAME "home"
#define MDNS_INSTANCE "esp32 home web server"

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static const char *TAG = "WIFI";

static TaskHandle_t init_task_handle;

static void initialise_mdns(void)
{
  ESP_ERROR_CHECK(mdns_init());
  ESP_ERROR_CHECK(mdns_hostname_set(MDNS_HOST_NAME));
  ESP_ERROR_CHECK(mdns_instance_name_set(MDNS_INSTANCE));

  ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
  ESP_ERROR_CHECK(mdns_service_instance_name_set("_http", "_tcp", "House monitor"));
  ESP_LOGI(TAG, "MDNS initialised.");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    ESP_ERROR_CHECK(esp_wifi_connect());
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    if (s_retry_num < MAXIMUM_RETRY)
    {
      ESP_ERROR_CHECK(esp_wifi_connect());
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    }
    else
    {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    xTaskNotifyGive(init_task_handle);
  }
  else
  {
    /* Do nothing for now */
  }
}

void wifi_init(TaskHandle_t init_task)
{
  esp_log_level_set(TAG, ESP_LOG_ERROR);
  init_task_handle=init_task;
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(nvs_flash_erase());
  esp_err_t ret = nvs_flash_init();
  ESP_ERROR_CHECK(ret);

  // Initialize lwip stack
  ESP_ERROR_CHECK(esp_netif_init());
  // Initialize event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Registering dns
  initialise_mdns();
  netbiosns_init();
  netbiosns_set_name(MDNS_HOST_NAME);

  // Wifi init
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  // Initialize station network interface with default settings
  esp_event_handler_instance_t inst_any_id;
  esp_event_handler_instance_t inst_got_ip;
  // Register wifi event
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      &inst_any_id));
  // Register connection to AP event
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      &inst_got_ip));
  http_server_config();

  wifi_config_t wifi_config = {
      .sta = {
          .ssid = WIFI_SSID,
          .password = WIFI_PASS}};

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "wifi_init_sta finished.");
}
