/*
 * Brief: Sensor data
 * author: Thomas Georgiadis
 */
#include "sensor_data.h"
#include "tc74.h"
#include "dht22.h"
#include "sntp_client.h"
#include "ldr.h"
#include "MQ135.h"
#include "sen0193.h"

static sensors_status status;

void status_update(void)
{
  sntp();
  sprintf(status.timestamp, get_timestamp());
  status.tc74_temp = temperature_reading();
  readDHT(&(status.humid), &(status.dht22_temp));
  status.co2 = get_co2_ppm_value();
  status.light = get_light_intensity();
  status.soil_moist = get_soil_humidity();
}

sensors_status get_status_update(void)
{
  return status;
}
