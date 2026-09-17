#pragma once
// Display/touch pin configuration for the Elecrow CrowPanel Advance 2.4"
// ESP32-S3 HMI board (model DIS01624A, hardware v1.1/v1.2).
//
// Values below are taken directly from Elecrow's official factory
// source (LovyanGFX_Driver.h) for this exact board:
// https://github.com/Elecrow-RD/CrowPanel-Advance-2.4-HMI-ESP32-320x240
//
// Panel:  ST7789, SPI, 240x320 native / used rotated as 320x240
// Touch:  FT6336 (FT5x06-compatible), I2C addr 0x38

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Touch_FT5x06 _touch_instance;

 public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 42;
      cfg.pin_mosi = 39;
      cfg.pin_miso = -1;
      cfg.pin_dc = 41;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 40;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;

      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 3;  // rotated to landscape 320x240
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = 239;
      cfg.y_min = 0;
      cfg.y_max = 319;
      cfg.pin_int = 47;
      cfg.bus_shared = false;
      cfg.offset_rotation = 6;

      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = 15;
      cfg.pin_scl = 16;
      cfg.freq = 400000;

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

// Backlight is a plain GPIO, not driven through LovyanGFX.
#define LCD_BACKLIGHT_PIN 38

// Logical (rotated) resolution used everywhere in the sketch.
#define LCD_H_RES 320
#define LCD_V_RES 240
