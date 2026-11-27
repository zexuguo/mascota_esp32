#pragma once
// ------------------------------------------------------------
// juegos.h — dos minijuegos con Clawd, en el mismo pixel art
//
//   VUELA: toca para que Clawd aletee y pase por los huecos entre las
//          columnas de bloques de código.
//   CORRE: Clawd corre solo; toca para saltar los bugs. Los voladores se
//          esquivan quedándose en el suelo. Cada vez va más rápido.
//
// Todo juego es el mismo bucle de 3 pasos, 25 veces por segundo:
//   1. leer lo que hace el jugador (un toque)
//   2. mover las cosas (física: velocidad += gravedad; posición += velocidad)
//   3. comprobar choques (¿se tocan dos rectángulos?) y dibujar
// ------------------------------------------------------------
#include "clawd.h"
#include "audio.h"
#include <Preferences.h>

extern Preferences ajustes;   // para guardar los récords

enum Juego { J_MENU, J_VUELA, J_CORRE, J_AJEDREZ };
void abrir_ajedrez();             // en ajedrez.h (la revisión de partidas)
extern uint8_t aj_estado;
extern float aj_acc[2];
enum Fase { F_LISTO, F_JUGANDO, F_FIN };
Juego juego = J_MENU;
Fase fase = F_LISTO;
int puntos = 0, record_vuela = 0, record_corre = 0;
bool record_nuevo = false;
uint32_t fin_desde = 0, juego_reloj = 0;

const Nota S_ALETEO[] = {{500, 800, 40, 20, 0}};
const Nota S_PUNTO[]  = {{1320, 1320, 45, 22, 0}, {1760, 1760, 70, 22, 0}};
const Nota S_CHOQUE[] = {{420, 110, 280, 70, 1}};
const Nota S_BRINCO[] = {{380, 760, 70, 22, 0}};
const Nota S_RECORD[] = {{784, 784, 80, 60, 1}, {1047, 1047, 80, 60, 1}, {1319, 1319, 160, 60, 1}};

// ¿se tocan dos rectángulos? (x, y, ancho, alto)
bool choca(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// Un número "al azar" que siempre sale igual para la misma semilla
uint32_t mezcla(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
  return x;
}

// Un bloque de "código": caja oscura con líneas de colores de distinto largo
void bloque_codigo(LGFX_Sprite &g, int x, int y, int w, int h, uint32_t semilla) {
  if (h <= 0 || w <= 0) return;
  x = x / L * L; y = y / L * L; w = (w + L - 1) / L * L; h = h / L * L;
  g.fillRect(x, y, w, h, C565(44, 40, 36));
  g.fillRect(x, y, L, h, GRIS_OSC);                // borde izquierdo
  g.fillRect(x + w - L, y, L, h, GRIS_OSC);        // borde derecho
  const uint16_t cols[] = {CLAWD, TXT2, TXT3, TXT3, TXT2};   // código: gris y el naranja de Claude
  for (int fy = y + L; fy + L <= y + h - L; fy += 2 * L) {
    uint32_t r = mezcla(semilla * 977 + fy);
    int sangria = (r % 3) * L;
    int largo = L * (1 + (r >> 4) % ((w - 3 * L - sangria) / L > 0 ? (w - 3 * L - sangria) / L : 1));
    g.fillRect(x + 2 * L + sangria, fy, largo, L, cols[(r >> 9) % 5]);
  }
}

// Clawd pequeño para los juegos (píxel de 2: 64 x 36 px)
void clawd_juego(LGFX_Sprite &g, float x, float pies, bool brazos_arriba, Piernas piernas, bool muerto, bool asustado) {
  Pose p;
  p.p = 2;
  p.x = x;
  p.suelo = pies;
  p.sombra = 0;
  p.piernas = piernas;
  p.brazo_i = p.brazo_d = brazos_arriba ? 1 : 0;
  p.ojos = muerto ? OJOS_CERRADOS : (asustado ? OJOS_GRANDES : OJOS_NORMAL);
  dibujar_clawd(g, p);
}

void texto_centrado(LGFX_Sprite &g, const char *t, int y_px, int p, uint16_t col) {
  int w = ancho_pixel(t, p);
  texto_pixel(g, t, (240 / p - w) / 2, y_px / p, p, col);
}

// =============================================================== VUELA
const int V_SUELO = 296, V_TECHO = 34;
const int V_X = 64;                     // dónde está Clawd (no se mueve en x)
const int COL_ANCHO = 36, HUECO = 108, SEPARACION = 150;
float v_y = 150, v_vel = 0;
uint32_t aleteo_hasta = 0;
struct Columna { float x; int hueco; bool contada; uint32_t semilla; };
Columna columnas[3];

void vuela_empezar() {
  v_y = 160; v_vel = 0;
  for (int i = 0; i < 3; i++)
    columnas[i] = {260.0f + i * SEPARACION, (int)random(100, 230), false, (uint32_t)random(100000)};
}

void vuela_aletear(uint32_t ahora) {
  v_vel = -6.0f;
  aleteo_hasta = ahora + 160;
  SONAR(S_ALETEO);
}

bool vuela_paso() {                     // devuelve false si choca
  v_vel = min(v_vel + 0.48f, 7.5f);     // gravedad (con velocidad máxima)
  v_y += v_vel;
  for (auto &c : columnas) {
    c.x -= 2.4f;
    if (c.x < -COL_ANCHO) {             // salió por la izquierda: vuelve por la derecha
      c.x += 3 * SEPARACION;
      c.hueco = random(100, 230);
      c.contada = false;
      c.semilla = random(100000);
    }
    if (!c.contada && c.x + COL_ANCHO < V_X - 22) {
      c.contada = true;
      puntos++;
      SONAR(S_PUNTO);
    }
  }
  // la caja de choque de Clawd: un poco más pequeña que el dibujo (es más justo)
  float cx = V_X - 20, cy = v_y - 15, cw = 40, ch = 28;
  if (v_y + 18 > V_SUELO || v_y - 18 < V_TECHO) return false;
  for (auto &c : columnas) {
    if (choca(cx, cy, cw, ch, c.x, V_TECHO, COL_ANCHO, c.hueco - HUECO / 2 - V_TECHO)) return false;
    if (choca(cx, cy, cw, ch, c.x, c.hueco + HUECO / 2, COL_ANCHO, V_SUELO - c.hueco - HUECO / 2)) return false;
  }
  return true;
}

void vuela_dibujar(LGFX_Sprite &g, uint32_t ahora) {
  // estrellitas del fondo, que van más despacio (parece que están lejos)
  for (int i = 0; i < 24; i++) {
    int x = (int)(mezcla(i) % 240 - (juego_reloj * 0.4f)) % 240;
    if (x < 0) x += 240;
    g.fillRect(x / 2 * 2, V_TECHO + (mezcla(i + 99) % 240), 2, 2, GRIS_OSC);
  }
  for (auto &c : columnas) {
    bloque_codigo(g, (int)c.x, V_TECHO, COL_ANCHO, c.hueco - HUECO / 2 - V_TECHO, c.semilla);
    bloque_codigo(g, (int)c.x, c.hueco + HUECO / 2, COL_ANCHO, V_SUELO - c.hueco - HUECO / 2, c.semilla + 1);
  }
  // suelo
  g.fillRect(0, V_SUELO, 240, 320 - V_SUELO, SUELO_COL);
  int desp = (int)(juego_reloj * 2.4f) % 12;
  for (int x = -desp; x < 240; x += 12) g.fillRect(x / 4 * 4, V_SUELO + 8, 4, 4, GRIS_OSC);
  bool muerto = fase == F_FIN;
  clawd_juego(g, V_X, v_y + 18, ahora < aleteo_hasta || fase == F_LISTO, PIERNAS_QUIETAS, muerto, v_vel > 5.5f);
}

// =============================================================== CORRE
const int C_SUELO = 262;
const int C_X = 52;
float c_alto = 0, c_vel = 0, velocidad = 3.2f, distancia = 0;
enum TipoObs : uint8_t { OBS_BUG, OBS_COLUMNA, OBS_VOLADOR };
struct Obstaculo { float x; TipoObs tipo; int alto_px; };
Obstaculo obs[3];

const char *const PAT_BUG[]    = {".X..X.", "..XX..", ".XXXX.", "XXXXXX", "X.XX.X"};
const char *const PAT_VOLA_A[] = {"X....X", ".XXXX.", "..XX.."};
const char *const PAT_VOLA_B[] = {".X..X.", "XXXXXX", "..XX.."};

void colocar_obstaculo(Obstaculo &o, float x) {
  o.x = x;
  int r = random(100);
  if (distancia > 1500 && r < 25) {
    o.tipo = OBS_VOLADOR;                      // vuela a la altura de la cabeza: no saltes
    o.alto_px = 12;
  } else if (r < 60) {
    o.tipo = OBS_BUG;
    o.alto_px = 20;
  } else {
    o.tipo = OBS_COLUMNA;
    o.alto_px = 32 + 8 * random(3);
  }
}

float obs_mas_lejos() {
  float m = 0;
  for (auto &o : obs) m = max(m, o.x);
  return m;
}

void corre_empezar() {
  c_alto = 0; c_vel = 0; velocidad = 3.2f; distancia = 0;
  for (int i = 0; i < 3; i++) colocar_obstaculo(obs[i], 280.0f + i * 200);
}

void corre_saltar() {
  if (c_alto > 0) return;                       // en el aire no se puede volver a saltar
  c_vel = -9.6f;
  SONAR(S_BRINCO);
}

// la caja de choque de cada obstáculo (x, y, ancho, alto)
void caja_obs(const Obstaculo &o, float &x, float &y, float &w, float &h) {
  switch (o.tipo) {
    case OBS_BUG:     x = o.x; y = C_SUELO - 20; w = 24; h = 20; break;
    case OBS_COLUMNA: x = o.x; y = C_SUELO - o.alto_px; w = 16; h = o.alto_px; break;
    case OBS_VOLADOR: x = o.x; y = C_SUELO - 64; w = 24; h = 12; break;
  }
}

bool corre_paso() {
  if (c_alto > 0 || c_vel < 0) {               // física del salto
    c_alto -= c_vel;
    c_vel += 0.72f;
    if (c_alto <= 0) { c_alto = 0; c_vel = 0; }
  }
  velocidad = min(8.0f, 3.2f + distancia / 1800);
  distancia += velocidad;
  int nuevos = (int)(distancia / 25);
  if (nuevos / 100 > puntos / 100) SONAR(S_PUNTO);   // cada 100 puntos
  puntos = nuevos;
  for (auto &o : obs) {
    o.x -= velocidad;
    if (o.x < -30) colocar_obstaculo(o, max(260.0f, obs_mas_lejos() + random(150, 260) + velocidad * 12));
  }
  float cx = C_X - 18, cy = C_SUELO - c_alto - 32, cw = 36, ch = 30;
  for (auto &o : obs) {
    float x, y, w, h;
    caja_obs(o, x, y, w, h);
    if (choca(cx, cy, cw, ch, x + 2, y + 2, w - 4, h - 4)) return false;
  }
  return true;
}

void corre_dibujar(LGFX_Sprite &g, uint32_t ahora) {
  // nubes de píxeles al fondo (se mueven más despacio)
  for (int i = 0; i < 4; i++) {
    int x = (int)(mezcla(i * 7) % 300 - distancia * 0.15f) % 300;
    if (x < -40) x += 300;
    int y = 70 + (mezcla(i * 7 + 1) % 90);
    caja_pixel(g, x / L, y / L, 10, 3, L, C565(40, 37, 34));
  }
  // suelo con piedrecitas que pasan
  g.fillRect(0, C_SUELO, 240, L, SUELO_COL);
  int desp = (int)distancia % 40;
  for (int x = -desp; x < 240; x += 40) {
    g.fillRect((x + 8) / 4 * 4, C_SUELO + 12, 4, 4, GRIS_OSC);
    g.fillRect((x + 28) / 4 * 4, C_SUELO + 24, 8, 4, GRIS_OSC);
  }
  for (auto &o : obs) {
    float x, y, w, h;
    caja_obs(o, x, y, w, h);
    switch (o.tipo) {
      case OBS_BUG:     patron(g, PAT_BUG, 5, (int)x, (int)y, L, ROJO); break;
      case OBS_COLUMNA: bloque_codigo(g, (int)x, (int)y, 16, (int)h, (uint32_t)o.alto_px * 31); break;
      case OBS_VOLADOR: patron(g, (ahora / 150) % 2 ? PAT_VOLA_A : PAT_VOLA_B, 3, (int)x, (int)y, L, TXT2); break;
    }
  }
  bool en_aire = c_alto > 0;
  Piernas pp = fase != F_JUGANDO || en_aire ? PIERNAS_QUIETAS : ((ahora / 90) % 2 ? PIERNAS_A : PIERNAS_B);
  clawd_juego(g, C_X, C_SUELO - c_alto, en_aire, pp, fase == F_FIN, false);
}

// =============================================================== MENÚ y control
// Botones del menú (en px de pantalla)
const int BOT_X = 24, BOT_W = 192, BOT1_Y = 72, BOT2_Y = 140, BOT3_Y = 208, BOT_H = 60;
const char *const PAT_PEON[] = {"..X..", ".XXX.", ".XXX.", "..X..", ".XXX.", "XXXXX", "XXXXX"};

void juegos_iniciar() {
  record_vuela = ajustes.getInt("rec_vuela", 0);
  record_corre = ajustes.getInt("rec_corre", 0);
}

void juego_empezar(Juego j) {
  juego = j;
  fase = F_LISTO;
  puntos = 0;
  record_nuevo = false;
  juego_reloj = 0;
  if (j == J_VUELA) vuela_empezar();
  if (j == J_CORRE) corre_empezar();
}

void juego_perder(uint32_t ahora) {
  fase = F_FIN;
  fin_desde = ahora;
  int &rec = juego == J_VUELA ? record_vuela : record_corre;
  if (puntos > rec) {
    rec = puntos;
    record_nuevo = true;
    ajustes.putInt(juego == J_VUELA ? "rec_vuela" : "rec_corre", rec);
    SONAR(S_RECORD);
  } else {
    SONAR(S_CHOQUE);
  }
}

// Un toque en la pantalla de juegos
void juego_toque(int x, int y, uint32_t ahora) {
  (void)x;
  switch (fase) {
    case F_LISTO:
    case F_JUGANDO:
      if (juego == J_MENU) {
        if (y >= BOT1_Y && y < BOT1_Y + BOT_H) juego_empezar(J_VUELA);
        else if (y >= BOT2_Y && y < BOT2_Y + BOT_H) juego_empezar(J_CORRE);
        else if (y >= BOT3_Y && y < BOT3_Y + BOT_H) abrir_ajedrez();
        return;
      }
      fase = F_JUGANDO;
      if (juego == J_VUELA) vuela_aletear(ahora);
      else corre_saltar();
      break;
    case F_FIN:
      if (ahora - fin_desde < 700) return;     // que no se reinicie sin querer
      if (y > 268) { juego = J_MENU; fase = F_LISTO; }
      else juego_empezar(juego);
      break;
  }
}

void juego_actualizar(uint32_t ahora) {
  if (juego == J_MENU || fase != F_JUGANDO) return;
  juego_reloj++;
  bool vivo = juego == J_VUELA ? vuela_paso() : corre_paso();
  if (!vivo) juego_perder(ahora);
}

void juego_dibujar(LGFX_Sprite &g, uint32_t ahora) {
  char b[64];
  if (juego == J_MENU) {
    texto_centrado(g, T("JUEGOS"), 44, L, CLAWD);
    // botón VUELA
    caja_pixel(g, BOT_X / L, BOT1_Y / L, BOT_W / L, BOT_H / L, L, PANEL);
    bool aleta = (ahora / 300) % 2;
    clawd_juego(g, 62, BOT1_Y + 46 - (aleta ? 4 : 0), aleta, PIERNAS_QUIETAS, false, false);
    texto_pixel(g, T("VUELA"), 100 / L, (BOT1_Y + 12) / L, L, CREMA);
    snprintf(b, sizeof(b), "%s %d", T("RECORD"), record_vuela);
    texto_pixel(g, b, 100 / 2, (BOT1_Y + 40) / 2, 2, GRIS);
    // botón CORRE
    caja_pixel(g, BOT_X / L, BOT2_Y / L, BOT_W / L, BOT_H / L, L, PANEL);
    clawd_juego(g, 62, BOT2_Y + 48, false, (ahora / 120) % 2 ? PIERNAS_A : PIERNAS_B, false, false);
    texto_pixel(g, T("CORRE"), 100 / L, (BOT2_Y + 12) / L, L, CREMA);
    snprintf(b, sizeof(b), "%s %d", T("RECORD"), record_corre);
    texto_pixel(g, b, 100 / 2, (BOT2_Y + 40) / 2, 2, GRIS);
    // botón AJEDREZ (la revisión de tus partidas de chess.com)
    caja_pixel(g, BOT_X / L, BOT3_Y / L, BOT_W / L, BOT_H / L, L, PANEL);
    patron(g, PAT_PEON, 7, 52, BOT3_Y + 16, L, CREMA);
    texto_pixel(g, T("AJEDREZ"), 100 / L, (BOT3_Y + 12) / L, L, CREMA);
    if (aj_estado == 2) snprintf(b, sizeof(b), "%s %d", T("PRECISION"), (int)aj_acc[0]);
    else snprintf(b, sizeof(b), "CHESS.COM");
    texto_pixel(g, b, 100 / 2, (BOT3_Y + 40) / 2, 2, GRIS);
    texto_centrado(g, T("DESLIZA PARA SALIR"), 284, 2, GRIS_OSC);
    return;
  }

  if (juego == J_VUELA) vuela_dibujar(g, ahora);
  else corre_dibujar(g, ahora);

  // puntos arriba a la derecha
  snprintf(b, sizeof(b), "%d", puntos);
  texto_pixel(g, b, 60 - 2 - ancho_pixel(b), 10, L, CREMA);

  if (fase == F_LISTO) {
    caja_pixel(g, 6, 26, 48, 15, L, PANEL);
    texto_centrado(g, juego == J_VUELA ? T("TOCA = ALETEAR") : T("TOCA = SALTAR"), 26 * L + 3 * L, 2, CREMA);
    texto_centrado(g, T("TOCA PARA EMPEZAR"), 26 * L + 9 * L, 2, GRIS);
  }
  if (fase == F_FIN) {
    caja_pixel(g, 5, 22, 50, 26, L, PANEL);
    texto_centrado(g, record_nuevo ? T("RECORD!") : T("FIN"), 25 * L, L, record_nuevo ? AMARILLO : CLAWD);
    snprintf(b, sizeof(b), "%s %d   %s %d", T("PUNTOS"), puntos, T("RECORD"), juego == J_VUELA ? record_vuela : record_corre);
    texto_centrado(g, b, 33 * L, 2, CREMA);
    if (ahora - fin_desde > 700) {
      texto_centrado(g, T("TOCA = OTRA VEZ"), 39 * L, 2, GRIS);
      caja_pixel(g, 18, 68, 24, 9, L, PANEL);
      texto_centrado(g, T("MENU"), 70 * L, L, CREMA);
    }
  }
}
