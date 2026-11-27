#pragma once
// ------------------------------------------------------------
// tactil.h — lee el táctil FT6336 por I2C (dirección 0x38)
// ------------------------------------------------------------
#include "placa.h"

bool tactil_iniciar() {
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);  delay(10);
  digitalWrite(PIN_TP_RST, HIGH); delay(300);
  Wire.beginTransmission(TP_ADDR);
  return Wire.endTransmission() == 0;
}

// true si hay un dedo; escribe su posición (en coordenadas de la pantalla)
bool tactil_leer(int16_t &x, int16_t &y) {
  Wire.beginTransmission(TP_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TP_ADDR, 5) != 5) return false;
  uint8_t d[5];
  for (int i = 0; i < 5; i++) d[i] = Wire.read();
  if ((d[0] & 0x0F) == 0) return false;
  x = ((d[1] & 0x0F) << 8) | d[2];
  y = ((d[3] & 0x0F) << 8) | d[4];
  x = 239 - x;   // el panel táctil está montado girado 180°
  y = 319 - y;
  return true;
}
