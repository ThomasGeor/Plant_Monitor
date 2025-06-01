idf_component_register(SRCS "sensor_data.c"
		       REQUIRES tc74 MQ135 ldr sen0193 dht22 sntp_client
		       INCLUDE_DIRS "include")
