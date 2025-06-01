#include "esp_log.h"
#include "esp_littlefs.h"
#include "file_system.h"

#define MAX_FILE_PATH_SIZE 40

static const char *TAG = "LIL_FS";
const char* fileSystemBasePath = "/littlefs/";

void init_file_system(void)
{
  esp_log_level_set(TAG, ESP_LOG_ERROR);
  esp_vfs_littlefs_conf_t conf =
  {
    .base_path = "/littlefs",
    .partition_label = "littlefs",
    .format_if_mount_failed = true,
    .dont_mount = false,
  };
  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to mount the filesystem %s", esp_err_to_name(err));
  }
  else
  {
    ESP_LOGI(TAG, "LittleFS mounted at /littlefs");
  }
}

esp_err_t get_file(const char* fileName, FILE** fp)
{
  esp_err_t file_read_status = ESP_OK;
  char abs_file_path[MAX_FILE_PATH_SIZE];
  snprintf(abs_file_path, sizeof(abs_file_path), "%s%s", fileSystemBasePath, fileName);
  *fp = fopen(abs_file_path, "r");
  if (fp==NULL)
  {
    file_read_status = ESP_FAIL;
  }
  ESP_LOGI(TAG, "%s", abs_file_path);
  return file_read_status;
}
