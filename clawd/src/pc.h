#pragma once
// ------------------------------------------------------------
// pc.h — recibe del PC una línea JSON por segundo (companion.py)
//   {"cpu":12,"ct":57,"ram":40,"bat":53,"carga":1,
//    "h":20,"m":15,"s":3,"dia":"jue 26 nov","claude":"trabajando"}
// y le manda eventos:  {"ev":"toque"}
// ------------------------------------------------------------
#include <Arduino.h>
#include <ArduinoJson.h>

float pc_cpu = 0, pc_temp = 0, pc_ram = 0;
int pc_bat = -1;            // -1 = el PC no tiene batería
bool pc_carga = true;
String pc_dia = "";
String pc_claude = "";      // "", "trabajando", "esperando", "terminado"
uint32_t pc_ultimo_dato = 0;

// Hora: el PC nos la da y nosotros seguimos contando con millis()
long pc_seg_base = -1;      // segundos desde medianoche al recibirla
uint32_t pc_seg_ms = 0;

String pc_linea;
bool pc_claude_voz = false; // el companion está ejecutando Claude por voz
bool pc_tapa = false;       // la tapa del portátil está cerrada
bool pc_insomne = false;    // el companion impide que el portátil se suspenda

// Lo define main.cpp: mensajes del PC que no son los datos de cada segundo
void al_mensaje(JsonDocument &doc);

bool pc_conectado() {
  return pc_ultimo_dato != 0 && millis() - pc_ultimo_dato < 5000;
}

// Segundos desde medianoche, o -1 si aún no sabemos la hora
long hora_seg() {
  if (pc_seg_base < 0) return -1;
  return (pc_seg_base + (millis() - pc_seg_ms) / 1000) % 86400;
}
int hora_h() { long s = hora_seg(); return s < 0 ? -1 : s / 3600; }

void pc_enviar(const char *evento) {
  if (!pc_conectado()) return;    // si nadie escucha, no escribir (no bloquear)
  Serial.printf("{\"ev\":\"%s\"}\n", evento);
}

void pc_enviar_linea(const String &json) {
  if (!pc_conectado()) return;
  Serial.print(json);
  Serial.print('\n');
}

void pc_actualizar() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n') {
      if (pc_linea.length() < 4000) pc_linea += c;
      continue;
    }
    JsonDocument doc;
    if (deserializeJson(doc, pc_linea) == DeserializationError::Ok) {
      if (!doc["cpu"].is<float>()) {     // no son los datos del segundo
        al_mensaje(doc);
        pc_linea = "";
        continue;
      }
      pc_cpu   = doc["cpu"] | pc_cpu;
      pc_temp  = doc["ct"] | pc_temp;
      pc_ram   = doc["ram"] | pc_ram;
      pc_bat   = doc["bat"] | pc_bat;
      pc_carga = (doc["carga"] | 1) != 0;
      pc_dia   = doc["dia"] | pc_dia.c_str();
      pc_claude = doc["claude"] | "";
      if (doc["h"].is<int>()) {
        pc_seg_base = doc["h"].as<int>() * 3600L + doc["m"].as<int>() * 60L + doc["s"].as<int>();
        pc_seg_ms = millis();
      }
      if (doc["voz"].is<int>()) pc_claude_voz = doc["voz"].as<int>() != 0;
      pc_tapa = (doc["tapa"] | 0) != 0;
      pc_insomne = (doc["insomne"] | 0) != 0;
      pc_ultimo_dato = millis();
    }
    pc_linea = "";
  }
}
