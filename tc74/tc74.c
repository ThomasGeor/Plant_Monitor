/*
 * Brief: TC74 driver
 * author : Thomas Georgiadis
*/

#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TC74_SLAVE_ADDR_A0 0x48
#define TC74_SLAVE_ADDR_A1 0x49
#define TC74_SLAVE_ADDR_A2 0x4A
#define TC74_SLAVE_ADDR_A3 0x4B
#define TC74_SLAVE_ADDR_A4 0x4C
#define TC74_SLAVE_ADDR_A5 0x4D /*!< default slave address for TC74 sensor */
#define TC74_SLAVE_ADDR_A6 0x4E

#define READ_TEMP_REGISTER 0x00
#define READ_WRITE_CONFIG_REGISTER 0x01
#define SET_NORM_OP_VALUE 0x00 /*!< sets the 7th bit of configuration register to normal mode */
#define SET_STANBY_VALUE 0x80  /*!< sets the 7th bit of configuration register to standby mode */

#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)

#define I2C_MASTER_SCL_IO GPIO_NUM_22 /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO GPIO_NUM_21 /*!< gpio number for I2C master data  */
#define I2C_MASTER_NUM I2C_NUMBER(0)  /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ 100000     /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0   /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0   /*!< I2C master doesn't need buffer */

#define TC74_SLAVE_ADDR TC74_SLAVE_ADDR_A5 /*!< slave address for TC74 sensor */
#define WRITE_BIT I2C_MASTER_WRITE         /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ           /*!< I2C master read */
#define ACK_CHECK_EN 0x1                   /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0                  /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                        /*!< I2C ack value */
#define NACK_VAL 0x1                       /*!< I2C nack value */

static void tc74_update(void* pvParam);

static const char *TEMP_TAG = "TC74";
static uint8_t temperature = 0;
SemaphoreHandle_t temperature_data_mutex;

/**
 * @brief i2c master initialization (esp32 as master)
 */

static esp_err_t i2c_master_init(void)
{
  int i2c_master_port = I2C_MASTER_NUM;
  i2c_config_t conf =
  {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = I2C_MASTER_SDA_IO,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_io_num = I2C_MASTER_SCL_IO,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = I2C_MASTER_FREQ_HZ
  };
  i2c_param_config(i2c_master_port, &conf);
  return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

/**
 * @brief read TC74 operation mode (I2C)
 *
 * param   i2c_num : Esp32's i2c port number to send data to.
 * param   mode : Operation mode returned by the sensor
 * return  err :  error code if anything went wrong
 *
 * Operations executed in the I2C bus
 * |-------|------------------------------|---------------------|-------|------------------------------|------------------|------|
 * | start | WR slave_addr + wr_bit + ack |   WR 1 byte + ack   | start | WR slave_addr + rd_bit + ack | RD 1 byte + nack | stop |
 * |-------|------------------------------|---------------------|-------|------------------------------|------------------|------|
 */
static esp_err_t i2c_master_read_tc74_config(i2c_port_t i2c_num, uint8_t *mode)
{
  int ret;
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, TC74_SLAVE_ADDR << 1 | WRITE_BIT, ACK_CHECK_EN);
  i2c_master_write_byte(cmd, READ_WRITE_CONFIG_REGISTER, ACK_CHECK_EN);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, TC74_SLAVE_ADDR << 1 | READ_BIT, ACK_CHECK_EN);
  i2c_master_read_byte(cmd, mode, NACK_VAL);
  i2c_master_stop(cmd);
  ret = i2c_master_cmd_begin(i2c_num, cmd, 300 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  return ret;
}

/**
 * @brief write data to TC74 configuration register, used for setting the sensor in normal/standby mode (I2C)
 *
 * param   i2c_num : Esp32's i2c port number to send data to.
 * param   mode : Operation mode you want to set -> NORMAL OR STANDBY
 * return  err :  error code if anything went wrong
 *
 * Operations executed in the I2C bus
 * |-------|------------------------------|-----------------|----------------------|------|
 * | start | WR slave_addr + wr_bit + ack | WR 1 byte + ack | WR data 1 byte + ack | stop |
 * |-------|------------------------------|-----------------|----------------------|------|
 */
static esp_err_t i2c_master_set_tc74_mode(i2c_port_t i2c_num, uint8_t mode)
{
  int ret;
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, TC74_SLAVE_ADDR << 1 | WRITE_BIT, ACK_CHECK_EN);
  i2c_master_write_byte(cmd, READ_WRITE_CONFIG_REGISTER, ACK_CHECK_EN);
  i2c_master_write_byte(cmd, mode, ACK_CHECK_EN);
  i2c_master_stop(cmd);
  ret = i2c_master_cmd_begin(i2c_num, cmd, 300 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  return ret;
}

/**
 * @brief read temperature data from TC74 sensor (I2C)
 *
 * param   i2c_num : i2c port number to send data to.
 * param   tmprt : Temperature value returned by the sensor
 * return  err :  error code if anything went wrong
 *
 * Operations executed in the I2C bus
 * |-------|---------------------------|---------------------|-------|---------------------------|--------------------|------|
 * | start | slave_addr + wr_bit + ack | write 1 byte + ack  | start | slave_addr + rd_bit + ack | read 1 byte + nack | stop |
 * |-------|---------------------------|---------------------|-------|---------------------------|--------------------|------|
 */
static esp_err_t i2c_master_read_temp(i2c_port_t i2c_num, uint8_t *tmprt)
{
  int ret;
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, TC74_SLAVE_ADDR << 1 | WRITE_BIT, ACK_CHECK_EN);
  i2c_master_write_byte(cmd, READ_TEMP_REGISTER, ACK_CHECK_EN);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, TC74_SLAVE_ADDR << 1 | READ_BIT, ACK_CHECK_EN);
  i2c_master_read_byte(cmd, tmprt, NACK_VAL);
  i2c_master_stop(cmd);
  ret = i2c_master_cmd_begin(i2c_num, cmd, 300 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  return ret;
}

void tc74_init(void)
{
  esp_log_level_set(TEMP_TAG, ESP_LOG_ERROR);
  ESP_ERROR_CHECK(i2c_master_init());
  xTaskCreate(tc74_update, "temperature_update_task", 4096, NULL, 5, NULL);
}

static void tc74_update(void* pvParam)
{
  temperature_data_mutex = xSemaphoreCreateMutex();
  if (temperature_data_mutex == NULL)
  {
    ESP_LOGE(TEMP_TAG, "Failed to create mutex");
    vTaskDelete(NULL);
  }
  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(10000));
    i2c_master_set_tc74_mode(I2C_MASTER_NUM, SET_NORM_OP_VALUE);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if(xSemaphoreTake(temperature_data_mutex, portMAX_DELAY) == pdTRUE)
    {
      i2c_master_read_temp(I2C_MASTER_NUM, &temperature);
      xSemaphoreGive(temperature_data_mutex);
    }
    ESP_LOGI(TEMP_TAG, "%d", temperature);
    // set standby mode for low consuption (5uA)
    i2c_master_set_tc74_mode(I2C_MASTER_NUM, SET_STANBY_VALUE);
  }
}

uint8_t temperature_reading(void)
{
  if(xSemaphoreTake(temperature_data_mutex, portMAX_DELAY) == pdTRUE)
  {
    xSemaphoreGive(temperature_data_mutex);
  }
  return temperature;
}
