idf_component_register(SRCS "http_server.c"
                       REQUIRES sensor_data esp_wifi esp_http_server sntp_client file_system json
                       INCLUDE_DIRS "include")
