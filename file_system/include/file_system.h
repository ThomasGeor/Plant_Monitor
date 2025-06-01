#include "esp_err.h"
#include <stdio.h>

void init_file_system(void);
esp_err_t get_file(const char* fileName, FILE** fp);
