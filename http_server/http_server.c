/*
 * Brief: Http local server creation and handling.
 * author: Thomas Georgiadis
 */
#include <stdio.h>
#include <cJSON.h>
#include "sys/param.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "file_system.h"
#include "sensor_data.h"

#define HTTP_STACK_SIZE 8192

static const char *TAG = "HTTP_SERVER";
static const char* WS_TAG = "WS";
static httpd_handle_t server_handler = NULL;
// Must change to support multiple clients
static int ws_data_fd = -1;
TaskHandle_t ws_update_task_handle = NULL;

static void connect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// Wifi connection handlers
static httpd_handle_t start_webserver(void);
static esp_err_t stop_webserver(httpd_handle_t server);

// Web server connection handlers
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err);
static esp_err_t get_favicon_handler(httpd_req_t *req);
static esp_err_t plant_status_get_handler(httpd_req_t *req);
static esp_err_t ws_data_handler(httpd_req_t *req);

// URI handlers
static const httpd_uri_t favicon_uri =
{
  .uri = "/favicon.ico",
  .method = HTTP_GET,
  .handler = get_favicon_handler,
  .user_ctx = "download favicon for the html pages"
};

static const httpd_uri_t plant_status_uri =
{
  .uri = "/plant_status",
  .method = HTTP_GET,
  .handler = plant_status_get_handler,
  .user_ctx = "status page"
};

static const httpd_uri_t ws_uri =
{
  .uri = "/ws_data",
  .method = HTTP_GET,
  .handler = ws_data_handler,
  .user_ctx = "websocket connection",
  .is_websocket = true
};

/* Function implementations*/

static esp_err_t send_json_status_data(httpd_req_t *req)
{
  esp_err_t return_status = ESP_FAIL;
  cJSON *root = cJSON_CreateObject();
  sensors_status data = get_status_update();
  cJSON_AddStringToObject(root, "timestamp", data.timestamp);
  cJSON_AddNumberToObject(root, "tc74_temp", data.tc74_temp);
  cJSON_AddNumberToObject(root, "dht22_temp",data.dht22_temp);
  cJSON_AddNumberToObject(root, "hum", data.humid);
  cJSON_AddNumberToObject(root, "co2", data.co2);
  cJSON_AddNumberToObject(root, "ldr", data.light);
  cJSON_AddNumberToObject(root, "soil_moist", data.soil_moist);

  const char *json_str = cJSON_PrintUnformatted(root);
  httpd_ws_frame_t frame =
  {
    .payload = (uint8_t *)json_str,
    .len = strlen(json_str),
    .type = HTTPD_WS_TYPE_TEXT
  };

  if(!req)
  {
    return_status = httpd_ws_send_frame_async(server_handler, ws_data_fd, &frame);
  }
  else
  {
    return_status = httpd_ws_send_frame(req, &frame);
  }

  if (return_status != ESP_OK)
  {
    ESP_LOGE("WS", "Send failed: %s", esp_err_to_name(return_status));
  }

  free((void *)json_str);
  cJSON_Delete(root);
  return return_status;
}

static void connect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  httpd_handle_t *server = (httpd_handle_t *)arg;
  if (*server == NULL)
  {
    ESP_LOGI(TAG, "Starting webserver");
    *server = start_webserver();
  }
}

static void disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  httpd_handle_t *server = (httpd_handle_t *)arg;
  if (*server)
  {
    ESP_LOGI(TAG, "Stopping webserver");
    if (stop_webserver(*server) == ESP_OK)
    {
      *server = NULL;
    }
    else
    {
      ESP_LOGE(TAG, "Failed to stop http server");
    }
  }
}

static esp_err_t get_favicon_handler(httpd_req_t *req)
{
  FILE* fp = NULL;
  esp_err_t return_status = get_file("favicon.ico", &fp);

  if(return_status == ESP_OK)
  {
    httpd_resp_set_type(req, "image/x-icon");
    char buff[128];
    size_t read_bytes = 0;
    while ((read_bytes = fread(buff, 1, sizeof(buff), fp)) > 0)
    {
      httpd_resp_send_chunk(req, buff, read_bytes);
    }
    fclose(fp);
    httpd_resp_send_chunk(req, NULL, 0); // EOF
  }
  else
  {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "favicon file could not be opened");
  }
  return return_status;
}

static esp_err_t plant_status_get_handler(httpd_req_t *req)
{
  FILE* fp = NULL;
  esp_err_t return_status = get_file("status_page.html", &fp);
  if(return_status == ESP_OK)
  {
    char line[128];
    httpd_resp_set_type(req, "text/html");
    while (fgets(line, sizeof(line), fp))
    {
        httpd_resp_sendstr_chunk(req, line);
    }
    fclose(fp);
    httpd_resp_sendstr_chunk(req, NULL); // EOF
  }
  else
  {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "HTML file could not be opened.");
  }
  return return_status;
}

// Asynchronous handling
static void sensors_update(void* pvParam)
{
  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(10000));
    if (httpd_ws_get_fd_info(server_handler, ws_data_fd) == HTTPD_WS_CLIENT_WEBSOCKET)
    {
      send_json_status_data(NULL);
    }
    else
    {
      ws_data_fd = -1;
      vTaskDelete(NULL);
    }
  }
}

static void sensors_update_start_task(void *arg)
{
  xTaskCreate(sensors_update, "sensors_update_task", 4096, NULL, 3, &ws_update_task_handle);
}

static esp_err_t ws_data_handler(httpd_req_t *req)
{
  esp_err_t return_status = ESP_FAIL;
  if (req->method == HTTP_GET)
  {
    // Websocket handshake and initialization
    if (httpd_req_get_hdr_value_len(req, "Upgrade") == 0)
    {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not a WebSocket request");
    }
    else
    {
      ws_data_fd = httpd_req_to_sockfd(req);
      if(ws_data_fd != -1)
      {
        ESP_LOGI(WS_TAG, "Websocket request at fd %d", ws_data_fd);
        httpd_queue_work(req->handle, sensors_update_start_task, NULL);
        return_status = ESP_OK;
      }
      else
      {
        ESP_LOGI(WS_TAG, "Failed to open websocket");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to open websocket");
      }
    }
  }
  else
  {
    httpd_ws_client_info_t fd_info = httpd_ws_get_fd_info(server_handler, ws_data_fd);
    if (fd_info == HTTPD_WS_CLIENT_WEBSOCKET)
    {
      // Web Request requests from the clinent
      httpd_ws_frame_t ws_pkt;
      memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
      ws_pkt.type = HTTPD_WS_TYPE_TEXT;
      return_status = httpd_ws_recv_frame(req, &ws_pkt, 0);
      if (return_status != ESP_OK)
      {
          ESP_LOGE(WS_TAG, "Failed to get frame len: %s", esp_err_to_name(return_status));
      }
      else if(ws_pkt.len)
      {
          uint8_t ws_rcv_buf[ws_pkt.len + 1];
          ws_pkt.payload = ws_rcv_buf;
          return_status = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
          ws_rcv_buf[ws_pkt.len] = 0;
          if(return_status != ESP_OK)
          {
            ESP_LOGE(WS_TAG, "Packet recption failed: %s", esp_err_to_name(return_status));
          }
          else if(strncmp((char*)ws_pkt.payload, "request_status", ws_pkt.len) == 0)
          {
            return_status = send_json_status_data(req);
          }
          else
          {
            ESP_LOGE(WS_TAG, "Got random packet: %s", ws_pkt.payload);
            return_status = ESP_OK;
          }
      }
      else
      {
          ESP_LOGE(WS_TAG, "Empty packet.");
      }
    }
    else
    {
      ESP_LOGE(WS_TAG, "Invalid websocket file descriptor");
    }
  }
  return return_status;
}

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
  esp_err_t return_status = ESP_FAIL;
  if (strcmp("/plant_status", req->uri) == 0)
  {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/plant_status URI is not available");
    /* Keep underlying socket open */
    return_status = ESP_OK;
  }
  else if (strcmp("/echo", req->uri) == 0)
  {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
  }
  else
  {
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
  }
  return return_status;
}

static httpd_handle_t start_webserver(void)
{
  esp_log_level_set(TAG, ESP_LOG_ERROR);
  esp_log_level_set(WS_TAG, ESP_LOG_ERROR);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = 5;
  config.lru_purge_enable = true;
  config.stack_size = HTTP_STACK_SIZE;

  ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
  if (httpd_start(&server_handler, &config) == ESP_OK)
  {
    httpd_register_uri_handler(server_handler, &favicon_uri);
    httpd_register_uri_handler(server_handler, &plant_status_uri);
    httpd_register_uri_handler(server_handler, &ws_uri);
  }
  else
  {
    ESP_LOGI(TAG, "Error starting server!");
  }
  return server_handler;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
  return httpd_stop(server);
}

void http_server_config(void)
{
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server_handler));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server_handler));
}
