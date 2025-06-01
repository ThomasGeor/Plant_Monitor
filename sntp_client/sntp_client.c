/* Brief: Communication driver with SNTP server and real time data calculation.
 * author: Thomas Georgiadis
 */
#include "esp_sntp.h"
#include "esp_log.h"

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

#define SNTP_TIME_SERVER "pool.ntp.org"
#define SNTP_MAX_RETRY_COUNT 15

static const char *TAG = "SNTP";
static time_t now = 0;
static struct tm timeinfo = {0};
static char timestamp_buf[64];

static void time_sync_notification_cb(struct timeval *tv)
{
  ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
  esp_log_level_set(TAG, ESP_LOG_ERROR);
  ESP_LOGI(TAG, "Initializing SNTP");

  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, SNTP_TIME_SERVER);
  esp_sntp_setservername(1, SNTP_TIME_SERVER); // set the secondary NTP server (will be used only if SNTP_MAX_SERVERS > 1)
  esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  esp_sntp_init();

  ESP_LOGI(TAG, "List of configured NTP servers:");

  for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i)
  {
    if (esp_sntp_getservername(i))
    {
      ESP_LOGI(TAG, "server %d: %s", i, esp_sntp_getservername(i));
    }
    else
    {
      // we have either IPv4 or IPv6 address, let's print it
      char buff[INET6_ADDRSTRLEN];
      ip_addr_t const *ip = esp_sntp_getserver(i);
      if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
        ESP_LOGI(TAG, "server %d : %s", i, buff);
    }
  }

  // wait for time to be set
  int retry = 0;
  const int retry_count = SNTP_MAX_RETRY_COUNT;
  while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count)
  {
    ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
  time(&now);
  localtime_r(&now, &timeinfo);
}

void sntp(void)
{
  time(&now);
  localtime_r(&now, &timeinfo);
  // Is time set? If not, tm_year will be (1970 - 1900).
  if (timeinfo.tm_year < (2016 - 1900))
  {
    initialize_sntp();
    time(&now);
  }
  // Set the timezone to Athens timezone
  setenv("TZ", "UTC-2", 1);
  tzset();
  localtime_r(&now, &timeinfo);
  strftime(timestamp_buf, sizeof(timestamp_buf), "%c", &timeinfo);
  ESP_LOGI(TAG, "The current date/time in Athens is : %s", timestamp_buf);
}

char *get_timestamp(void)
{
  return timestamp_buf;
}
