#pragma once
// ------------------------------------------------------------
// audio.h — sonidos de 8 bits y micrófono, en una TAREA aparte
//
// El audio corre en el núcleo 0 (FreeRTOS) para que la animación
// (núcleo 1, el loop) no se congele mientras suena algo.
// - sonar(notas): mete notas en una cola; la tarea las toca.
// - mic_pico: la tarea, cuando no suena nada, escucha el micro y
//   guarda aquí el volumen más alto (RMS) desde la última lectura.
// ------------------------------------------------------------
#include "placa.h"
#include <ESP_I2S.h>

I2SClass i2s;

// Una nota: desliza de f0 a f1 Hz en 'ms' milisegundos. f0 = 0 -> silencio.
// onda: 0 = cuadrada (sonido "chiptune"), 1 = triangular (más suave)
struct Nota {
  uint16_t f0, f1, ms;
  uint8_t vol, onda;
};

QueueHandle_t cola_sonido;
volatile float mic_pico = 0;
volatile bool audio_mudo = false;
volatile uint32_t ignorar_mic_hasta = 0;
volatile bool mic_mudo = false;         // micro apagado: ni escucha ni graba

// Grabación de voz: la tarea de audio mete aquí las muestras (mono) y el
// loop las va sacando para mandarlas al PC. Es un "búfer circular":
// cab = dónde escribe la tarea, cola = dónde lee el loop.
volatile bool grabando = false;
const uint32_t GRAB_N = 16384;            // ~1 s de margen
int16_t grab_buf[GRAB_N];
volatile uint32_t grab_cab = 0, grab_cola = 0;

int grab_sacar(int16_t *dst, int max) {
  uint32_t hay = grab_cab - grab_cola;
  int n = min((uint32_t)max, hay);
  for (int i = 0; i < n; i++) dst[i] = grab_buf[(grab_cola + i) % GRAB_N];
  grab_cola += n;
  return n;
}

#define SONAR(arr) sonar(arr, sizeof(arr) / sizeof(arr[0]))

void sonar(const Nota *notas, int n) {
  if (audio_mudo || !cola_sonido) return;
  for (int i = 0; i < n; i++) xQueueSend(cola_sonido, &notas[i], 0);
}

static void codec_escribir(uint8_t reg, uint8_t valor) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(valor);
  Wire.endTransmission();
}

static void tocar(const Nota &n) {
  static int16_t buf[256 * 2];
  const int total = SAMPLE_RATE * n.ms / 1000;
  float fase = 0;
  for (int i = 0; i < total;) {
    int trozo = min(256, total - i);
    for (int k = 0; k < trozo; k++, i++) {
      float s = 0;
      if (n.f0 > 0) {
        float f = n.f0 + (float)(n.f1 - n.f0) * i / total;
        fase += f / SAMPLE_RATE;
        if (fase >= 1) fase -= 1;
        s = (n.onda == 0) ? (fase < 0.5f ? 1.f : -1.f)       // cuadrada
                          : 4.f * fabsf(fase - 0.5f) - 1.f;  // triangular
      }
      float env = min(1.0f, min(i, total - i) / 120.0f);     // sin "clics"
      int16_t v = (int16_t)(s * env * n.vol * 128);
      buf[2 * k] = v;
      buf[2 * k + 1] = v;
    }
    i2s.write((uint8_t *)buf, trozo * 4);
  }
}

static void tarea_audio(void *) {
  static int16_t mbuf[128 * 2];
  Nota n;
  for (;;) {
    if (xQueueReceive(cola_sonido, &n, 0) == pdTRUE) {
      tocar(n);
      ignorar_mic_hasta = millis() + 250;   // que no se asuste de sí mismo
      continue;
    }
    size_t muestras = i2s.readBytes((char *)mbuf, sizeof(mbuf)) / 2;
    if (muestras == 0) { vTaskDelay(1); continue; }
    if (mic_mudo) continue;               // se lee igual (para vaciar), pero se tira
    if (grabando) {
      for (size_t j = 0; j + 1 < muestras; j += 2) {   // estéreo -> mono
        if (grab_cab - grab_cola >= GRAB_N) break;     // lleno: perder antes que bloquear
        grab_buf[grab_cab % GRAB_N] = (int16_t)(((int32_t)mbuf[j] + mbuf[j + 1]) / 2);
        grab_cab++;
      }
    }
    if (millis() < ignorar_mic_hasta) continue;
    double acc = 0;
    for (size_t k = 0; k < muestras; k++) acc += (double)mbuf[k] * mbuf[k];
    float rms = sqrt(acc / muestras);
    if (rms > mic_pico) mic_pico = rms;
  }
}

bool audio_iniciar() {
  Wire.beginTransmission(ES8311_ADDR);
  if (Wire.endTransmission() != 0) return false;

  // Receta del ES8311 (driver de Espressif) para 16 kHz / 16 bits
  codec_escribir(0x00, 0x1F); delay(20); codec_escribir(0x00, 0x00);
  codec_escribir(0x45, 0x00);
  codec_escribir(0x01, 0x3F);
  codec_escribir(0x02, 0x00);
  codec_escribir(0x03, 0x10);
  codec_escribir(0x16, 0x24);
  codec_escribir(0x04, 0x10);
  codec_escribir(0x05, 0x00);
  codec_escribir(0x06, 0x03);
  codec_escribir(0x07, 0x00);
  codec_escribir(0x08, 0xFF);
  codec_escribir(0x0B, 0x00);
  codec_escribir(0x0C, 0x00);
  codec_escribir(0x10, 0x1F);
  codec_escribir(0x11, 0x7F);
  codec_escribir(0x00, 0x80);
  codec_escribir(0x09, 0x0C);
  codec_escribir(0x0A, 0x0C);
  codec_escribir(0x0D, 0x01);
  codec_escribir(0x0E, 0x02);
  codec_escribir(0x12, 0x00);
  codec_escribir(0x13, 0x10);
  codec_escribir(0x14, 0x1A);
  codec_escribir(0x15, 0x40);
  codec_escribir(0x17, 0xBF);
  codec_escribir(0x1C, 0x6A);
  codec_escribir(0x37, 0x08);
  codec_escribir(0x32, 0xA8);   // volumen del altavoz (algo bajado: con mucho volumen, la placa se queda sin corriente)

  pinMode(PIN_AMP_EN, OUTPUT);
  digitalWrite(PIN_AMP_EN, LOW);

  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO))
    return false;
  i2s.setTimeout(20);

  cola_sonido = xQueueCreate(40, sizeof(Nota));
  xTaskCreatePinnedToCore(tarea_audio, "audio", 4096, nullptr, 2, nullptr, 0);
  return true;
}

// ---------- Repertorio de Clawd ----------
const Nota S_HOLA[]      = {{523, 523, 70, 28, 0}, {0, 0, 30, 0, 0}, {784, 784, 80, 28, 0}, {1047, 1047, 130, 28, 0}};
const Nota S_SALTO[]     = {{500, 1100, 90, 26, 0}, {1100, 1400, 60, 20, 0}};
const Nota S_SUSTO[]     = {{300, 1800, 160, 30, 0}};
const Nota S_MIMOS[]     = {{140, 190, 180, 90, 1}, {190, 150, 240, 90, 1}};
const Nota S_LISTO[]     = {{659, 659, 80, 70, 1}, {784, 784, 80, 70, 1}, {1047, 1047, 80, 70, 1}, {1319, 1319, 80, 70, 1}, {1568, 1568, 240, 70, 1}};
const Nota S_ESPERA[]    = {{880, 880, 90, 24, 0}, {0, 0, 70, 0, 0}, {1175, 1175, 120, 24, 0}};
const Nota S_DORMIR[]    = {{600, 250, 450, 60, 1}};
const Nota S_DESPERTAR[] = {{250, 700, 220, 60, 1}};
const Nota S_POMO_INI[]  = {{784, 784, 90, 60, 1}, {1047, 1047, 160, 60, 1}};
const Nota S_POMO_FIN[]  = {{1047, 1047, 110, 70, 1}, {784, 784, 110, 70, 1}, {1047, 1047, 110, 70, 1}, {1319, 1319, 260, 70, 1}};
const Nota S_RECORDAR[]  = {{988, 988, 100, 60, 1}, {1319, 1319, 180, 60, 1}};
const Nota S_CLIC[]      = {{1200, 1200, 25, 18, 0}};
const Nota S_ESCUCHO[]   = {{880, 1320, 70, 50, 1}};
const Nota S_OIDO[]      = {{1320, 880, 70, 50, 1}};
const Nota S_ERROR[]     = {{300, 200, 200, 60, 1}};
