#include <Arduino.h>      // las funciones de Arduino (Serial, delay, pinMode...)

void setup() {
  Serial.begin(115200);   // abre la comunicación con el PC a 115200 baudios
}

void loop() {
  Serial.println("Hola desde la ESP32!");
  delay(1000);            // espera 1000 milisegundos = 1 segundo
}