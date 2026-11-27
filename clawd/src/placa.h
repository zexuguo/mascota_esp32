#pragma once
// ------------------------------------------------------------
// placa.h — todo lo que depende del hardware (pines y pantalla)
// Placa: LCDWiki ES3C28P (ESP32-S3 + ILI9341 240x320 + FT6336 + ES8311)
// ------------------------------------------------------------
#include <Arduino.h>
#include <LovyanGFX.hpp>   // SIEMPRE antes que ESP_I2S.h (choque de i2s_port_t)
#include <Wire.h>

// Pantalla (SPI)
#define PIN_LCD_CS    10
#define PIN_LCD_DC    46
#define PIN_LCD_SCK   12
#define PIN_LCD_MOSI  11
#define PIN_LCD_MISO  13
#define PIN_LCD_BL    45

// I2C (táctil + códec de audio)
#define PIN_I2C_SDA   16
#define PIN_I2C_SCL   15
#define PIN_TP_RST    18
#define PIN_TP_INT    17
#define TP_ADDR       0x38

// Audio (I2S). OJO: DOUT=8 y DIN=6 (la doc del fabricante lo pone al revés)
#define PIN_I2S_MCLK  4
#define PIN_I2S_BCLK  5
#define PIN_I2S_WS    7
#define PIN_I2S_DOUT  8
#define PIN_I2S_DIN   6
#define PIN_AMP_EN    1     // LOW = amplificador encendido
#define ES8311_ADDR   0x18
#define SAMPLE_RATE   16000

// LED de colores WS2812
#define PIN_RGB       42

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;

public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host    = SPI2_HOST;
      c.spi_mode    = 0;
      c.freq_write  = 40000000;
      c.freq_read   = 16000000;
      c.pin_sclk    = PIN_LCD_SCK;
      c.pin_mosi    = PIN_LCD_MOSI;
      c.pin_miso    = PIN_LCD_MISO;
      c.pin_dc      = PIN_LCD_DC;
      c.dma_channel = SPI_DMA_CH_AUTO;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs       = PIN_LCD_CS;
      c.pin_rst      = -1;
      c.pin_busy     = -1;
      c.panel_width  = 240;
      c.panel_height = 320;
      c.readable     = true;
      c.invert       = true;    // pantalla IPS
      c.rgb_order    = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl      = PIN_LCD_BL;
      c.invert      = false;
      c.freq        = 44100;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
