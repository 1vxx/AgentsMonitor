// The CO5300 protocol, as Arduino_GFX speaks it: a register write is opcode
// 0x02 with the register in the middle byte of the 24-bit address, all on one
// line; pixels follow a RAMWR as opcode 0x32, address 0x003C00, in QIO.

#include "panel.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace panel {
namespace {

// The 1.75C's wiring. The plain 1.75 resets the panel on GPIO39 instead.
constexpr gpio_num_t PIN_CS = GPIO_NUM_12, PIN_RST = GPIO_NUM_1;
constexpr int PIN_SCK = 38, PIN_D0 = 4, PIN_D1 = 5, PIN_D2 = 6, PIN_D3 = 7;
constexpr int W = 466, H = 466;
constexpr int X_OFFSET = 6;  // the panel's columns start at 6 of the controller's 480
constexpr int CLOCK_HZ = 40 * 1000 * 1000;
constexpr int MAX_TRANSFER = 32768;  // bytes; a band goes in one or two transfers
constexpr int QUEUE = 2;

spi_device_handle_t dev;
spi_transaction_ext_t pending[QUEUE];
int queued;
int sending = -1;  // the band buffer on its way out, if any

void reg(uint8_t r, const uint8_t *data = nullptr, int len = 0) {
  spi_transaction_t t = {};
  t.cmd = 0x02;
  t.addr = uint32_t(r) << 8;
  if (len) {
    t.flags = SPI_TRANS_USE_TXDATA;  // a bare command must have neither this nor a buffer
    memcpy(t.tx_data, data, len);
  }
  t.length = len * 8;
  gpio_set_level(PIN_CS, 0);
  spi_device_polling_transmit(dev, &t);
  gpio_set_level(PIN_CS, 1);
}

void reg8(uint8_t r, uint8_t v) { reg(r, &v, 1); }

void reg16x2(uint8_t r, uint16_t a, uint16_t b) {
  const uint8_t d[4] = {uint8_t(a >> 8), uint8_t(a), uint8_t(b >> 8), uint8_t(b)};
  reg(r, d, 4);
}

}  // namespace

bool begin() {
  gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_CS, 1);
  gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_RST, 1);
  delay(10);
  gpio_set_level(PIN_RST, 0);
  delay(200);
  gpio_set_level(PIN_RST, 1);
  delay(200);

  spi_bus_config_t bus = {};
  bus.data0_io_num = PIN_D0;
  bus.data1_io_num = PIN_D1;
  bus.data2_io_num = PIN_D2;
  bus.data3_io_num = PIN_D3;
  bus.sclk_io_num = PIN_SCK;
  bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
  bus.max_transfer_sz = MAX_TRANSFER;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  spi_device_interface_config_t cfg = {};
  cfg.command_bits = 8;
  cfg.address_bits = 24;
  cfg.mode = 0;
  cfg.clock_speed_hz = CLOCK_HZ;
  cfg.spics_io_num = -1;  // CS stays low across a frame's transfers, so it is ours
  cfg.flags = SPI_DEVICE_HALFDUPLEX;
  cfg.queue_size = QUEUE;
  if (spi_bus_add_device(SPI2_HOST, &cfg, &dev) != ESP_OK) return false;

  reg(0x11);  // sleep out
  delay(120);
  reg8(0xFE, 0x00);
  reg8(0xC4, 0x80);  // SPI mode control
  reg8(0x3A, 0x55);  // 16 bits a pixel
  reg8(0x53, 0x20);
  reg8(0x63, 0xFF);  // HBM brightness
  reg(0x29);         // display on
  reg8(0x51, 0xD0);
  reg8(0x58, 0x00);  // contrast enhancement off
  delay(10);
  reg(0x20);         // inversion off
  reg8(0x36, 0x00);  // RGB, no flips
  reg16x2(0x2A, X_OFFSET, X_OFFSET + W - 1);
  return true;
}

void brightness(uint8_t level) { reg8(0x51, level); }

// Waits for the band on its way out, then lets the panel's CS go
void finish() {
  spi_transaction_t *done;
  while (queued) spi_device_get_trans_result(dev, &done, portMAX_DELAY), queued--;
  if (sending >= 0) gpio_set_level(PIN_CS, 1), sending = -1;
}

void send(int k, const uint16_t *band, int y0, int rows) {
  // The window is set by polled register writes, which need the bus idle
  finish();
  reg16x2(0x2B, y0, y0 + rows - 1);  // RASET
  reg(0x2C);                          // RAMWR
  const uint8_t *p = (const uint8_t *)band;
  size_t left = size_t(rows) * W * 2;
  gpio_set_level(PIN_CS, 0);
  sending = k;
  for (int n = 0; left && n < QUEUE; n++) {
    spi_transaction_ext_t &x = pending[n];
    x = {};
    size_t len = left > MAX_TRANSFER ? MAX_TRANSFER : left;
    if (n == 0) {
      x.base.flags = SPI_TRANS_MODE_QIO;
      x.base.cmd = 0x32;
      x.base.addr = 0x003C00;
    } else {
      // Bare pixels continuing the same write
      x.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    }
    x.base.tx_buffer = p;
    x.base.length = len * 8;
    // A transfer that was refused never comes back, so never wait for it
    if (spi_device_queue_trans(dev, &x.base, portMAX_DELAY) != ESP_OK) break;
    queued++;
    p += len;
    left -= len;
  }
}

void wait(int k) {
  if (sending == k) finish();
}

}  // namespace panel
