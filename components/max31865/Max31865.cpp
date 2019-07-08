#include "Max31865.h"
#include <esp_log.h>
#include <freertos/task.h>
#include <math.h>
#include <climits>

static const char *TAG = "Max31865";

Max31865::Max31865(int miso, int mosi, int sck, int cs)
    : miso(miso), mosi(mosi), sck(sck), cs(cs) {}

Max31865::~Max31865() {
  spi_bus_remove_device(deviceHandle);
  spi_bus_free(hostDevice);
}

esp_err_t Max31865::begin(max31865_config_t config, max31865_rtd_config_t rtd) {
  spi_bus_config_t busConfig;
  busConfig.miso_io_num = miso;
  busConfig.mosi_io_num = mosi;
  busConfig.sclk_io_num = sck;
  busConfig.quadhd_io_num = -1;
  busConfig.quadwp_io_num = -1;
  esp_err_t err = spi_bus_initialize(hostDevice, &busConfig, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error initialising SPI bus: %d", err);
    return err;
  }

  spi_device_interface_config_t deviceConfig;
  deviceConfig.spics_io_num = cs;
  deviceConfig.clock_speed_hz = 5000000;
  deviceConfig.address_bits = CHAR_BIT;
  err = spi_bus_add_device(hostDevice, &deviceConfig, &deviceHandle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error adding SPI device: %d", err);
    return err;
  }
  rtdConfig = rtd;
  return setConfig(config);
}

esp_err_t Max31865::writeSPI(uint8_t addr, uint8_t *data, size_t size) {
  spi_transaction_t transaction;
  transaction.length = CHAR_BIT * size;
  transaction.rxlength = 0;
  transaction.addr = addr | MAX31865_REG_WRITE_OFFSET;
  transaction.tx_buffer = data;
  transaction.rx_buffer = nullptr;
  return spi_device_transmit(deviceHandle, &transaction);
}

esp_err_t Max31865::readSPI(uint8_t addr, uint8_t *result, size_t size) {
  spi_transaction_t transaction;
  transaction.length = CHAR_BIT * size;
  transaction.addr = addr & (MAX31865_REG_WRITE_OFFSET - 1);
  transaction.tx_buffer = nullptr;
  transaction.rx_buffer = result;
  transaction.rxlength = size;
  return spi_device_transmit(deviceHandle, &transaction);
}

esp_err_t Max31865::setConfig(max31865_config_t config) {
  chipConfig = config;
  uint8_t configByte = 0;
  if (config.vbias) {
    configByte |= 1UL << MAX31865_CONFIG_VBIAS_BIT;
  }
  if (config.autoConversion) {
    configByte |= 1UL << MAX31865_CONFIG_CONVERSIONMODE_BIT;
  }
  if (config.nWires == Max31865NWires::Three) {
    configByte |= 1UL << MAX31865_CONFIG_NWIRES_BIT;
  }
  if (config.faultDetection != Max31865FaultDetection::NoAction) {
    configByte |= static_cast<uint8_t>(config.faultDetection)
                  << MAX31865_CONFIG_FAULTDETECTION_BIT;
  }
  if (config.filter != Max31865Filter::Hz60) {
    configByte |= 1UL << MAX31865_CONFIG_MAINSFILTER_BIT;
  }
  return writeSPI(MAX31865_CONFIG_REG, &configByte, 1);
}

esp_err_t Max31865::getConfig(max31865_config_t *config) {
  uint8_t configByte = 0;
  esp_err_t err = readSPI(MAX31865_CONFIG_REG, &configByte, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error reading config: %d", err);
    return err;
  }
  config->vbias = ((configByte >> MAX31865_CONFIG_VBIAS_BIT) & 1U) != 0;
  config->autoConversion =
      ((configByte >> MAX31865_CONFIG_CONVERSIONMODE_BIT) & 1U) != 0;
  config->nWires = static_cast<Max31865NWires>(
      (configByte >> MAX31865_CONFIG_NWIRES_BIT) & 1U);
  config->faultDetection = static_cast<Max31865FaultDetection>(
      (configByte >> MAX31865_CONFIG_FAULTDETECTION_BIT) & 1U);
  config->filter = static_cast<Max31865Filter>(
      (configByte >> MAX31865_CONFIG_MAINSFILTER_BIT) & 1U);
  return ESP_OK;
}

esp_err_t Max31865::clearFault() {
  uint8_t configByte = 0;
  esp_err_t err = readSPI(MAX31865_CONFIG_REG, &configByte, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error reading config: %d", err);
    return err;
  }
  configByte &= ~(1U << MAX31865_CONFIG_FAULTSTATUS_BIT);
  return writeSPI(MAX31865_CONFIG_REG, &configByte, 1);
}

esp_err_t Max31865::readFaultStatus(uint8_t *fault) {
  *fault = 0;
  return readSPI(MAX31865_FAULT_STATUS_REG, fault, 1);
}

esp_err_t Max31865::getRTD(uint16_t *rtd) {
  max31865_config_t oldConfig = chipConfig;
  if (!chipConfig.vbias) {
    chipConfig.vbias = true;
    esp_err_t err = setConfig(chipConfig);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Error setting config: %d", err);
      return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (!chipConfig.autoConversion) {
    uint8_t configByte = 0;
    esp_err_t err = readSPI(MAX31865_CONFIG_REG, &configByte, 1);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Error reading config: %d", err);
      return err;
    }
    configByte |= 1U << MAX31865_CONFIG_1SHOT_BIT;
    err = writeSPI(MAX31865_CONFIG_REG, &configByte, 1);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Error writing config: %d", err);
      return err;
    }
    vTaskDelay(pdMS_TO_TICKS(65));
  }

  uint8_t rtdBytes[2];
  esp_err_t err = readSPI(MAX31865_RTD_REG, rtdBytes, 2);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error reading RTD: %d", err);
    return err;
  }

  *rtd = rtdBytes[0] << CHAR_BIT;
  *rtd |= rtdBytes[1];
  *rtd >>= 1U;

  return setConfig(oldConfig);
}

esp_err_t Max31865::getTemperature(float *temperature) {
  uint16_t rtd = 0;
  esp_err_t err = getRTD(&rtd);
  if (err != ESP_OK) {
    return err;
  }
  float Rrtd = (rtd * rtdConfig.ref) / (1U << 15U);

  // Callendar-Van Dusen
  float Z1, Z2, Z3, Z4;
  static constexpr float RTD_A = 3.9083e-3;
  static constexpr float RTD_B = -5.775e-7;
  Z1 = -RTD_A;
  Z2 = RTD_A * RTD_A - (4 * RTD_B);
  Z3 = (4 * RTD_B) / rtdConfig.nominal;
  Z4 = 2 * RTD_B;
  *temperature = Z2 + (Z3 * Rrtd);
  *temperature = (sqrt(*temperature) + Z1) / Z4;

  if (*temperature > 0.0) {
    return ESP_OK;
  }

  // Analog Devices AN709 polynomial
  Rrtd /= rtdConfig.nominal;
  Rrtd *= 100.0;
  static constexpr float A[6] = {-242.02,   2.2228,    2.5859e-3,
                                 4.8260e-6, 2.8183e-8, 1.5243e-10};
  *temperature = A[0] + A[1] * Rrtd + A[2] * pow(Rrtd, 2) +
                 A[3] * pow(Rrtd, 3) + A[4] * pow(Rrtd, 4) +
                 A[5] * pow(Rrtd, 5);

  return ESP_OK;
}
