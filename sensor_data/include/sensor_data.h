#include <stdio.h>
typedef struct
{
  char timestamp[20];
  uint8_t tc74_temp;
  float dht22_temp;
  float humid;
  float co2;
  uint8_t light;
  uint8_t soil_moist;
}sensors_status;

void status_update(void);
sensors_status get_status_update(void);
