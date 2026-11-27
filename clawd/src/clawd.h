#pragma once
// ------------------------------------------------------------
// clawd.h — cómo se DIBUJA Clawd y todo lo que se mueve a su alrededor
//
// TODO es pixel art sobre UNA SOLA REJILLA: cada "píxel" de la escena mide
// L = 4 píxeles de verdad. Clawd, los corazones, el confeti, las letras de
// los bocadillos... todos usan el mismo tamaño de píxel, y todas las
// posiciones se redondean a esa rejilla. Así nada desentona.
//
// Clawd en píxeles de la rejilla, con las proporciones exactas del de Claude Code
// (allí es " ▐▛███▜▌ / ▝▜█████▛▘ / ▘▘ ▝▝ ": una cuadrícula de cuartos de celda,
// y cada cuarto de la terminal = 2 x 4 píxeles de aquí):
//
//        ........................      <- cuerpo 24 x 16
//        ....OO............OO....      <- ojos 2 x 4 (filas 4-7)
//    BBBB........................BBBB  <- brazos 4 x 4 (filas 8-11)
//        PP..PP............PP..PP      <- piernas 2 x 4, en las esquinas
// ------------------------------------------------------------
#include "placa.h"
#include "idiomas.h"

#define C565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// ---- Paleta: base neutra; el color solo para identidad (Clawd) y estados ----
// Superficies: cada escalón, un poco más claro (la jerarquía va por tono, no por bloques)
const uint16_t FONDO      = C565(14, 14, 13);
const uint16_t SUP        = C565(28, 27, 25);    // tarjetas y paneles
const uint16_t SUP2       = C565(42, 40, 38);    // elemento activo / pistas de barras
const uint16_t BORDE      = C565(58, 55, 52);    // bordes de 1 px
const uint16_t SUELO_COL  = C565(44, 42, 39);
const uint16_t SOMBRA     = C565(6, 6, 6);
// Texto, en tres escalones
const uint16_t TXT        = C565(236, 233, 226);
const uint16_t TXT2       = C565(156, 150, 142);
const uint16_t TXT3       = C565(98, 94, 89);
// Identidad: el naranja de Claude (Clawd, el spinner, la marca)
const uint16_t CLAWD      = C565(215, 119, 87);  // clawd_body de Claude Code
const uint16_t CLAWD_CALOR = C565(232, 92, 66);
const uint16_t OJO        = C565(10, 10, 10);
// Estados (significado universal): bien, espera, error
const uint16_t VERDE      = C565(112, 190, 110);
const uint16_t AMARILLO   = C565(232, 190, 80);
const uint16_t ROJO       = C565(222, 84, 68);
// Nombres antiguos, para no romper nada
const uint16_t PANEL = SUP, CREMA = TXT, GRIS = TXT2, GRIS_OSC = TXT3;

// ---- Letras: dos familias y nada más ----
//  - píxel 3x5 (texto_pixel) para títulos, bocadillos y juegos
//  - monoespaciada efont (con tildes, ñ y chino simplificado) para todo el texto
// Son variables: según el idioma, efontJA (latín con tildes, ñ, ç, l·l) o efontCN
// (chino simplificado). Las cambia letras_para() al elegir idioma.
const lgfx::IFont *F_TXT = &fonts::efontJA_14;
const lgfx::IFont *F_PEQ = &fonts::efontJA_12;
const lgfx::IFont *F_GRANDE = &fonts::efontJA_16;

// Para textos que vienen de fuera (resúmenes, comandos, nombres de proyecto...):
// la letra china solo si el texto tiene chino; si no, la latina (que tiene ñ, tildes, ç).
const lgfx::IFont *letra_para(const char *t, const lgfx::IFont *tam) {
  bool chino = false;
  for (const char *c = t; *c; c++) if ((uint8_t)*c >= 0xE2 && (uint8_t)*c != 0xE2) { chino = true; break; }
  int px = tam == F_GRANDE ? 16 : tam == F_PEQ ? 12 : 14;
  if (chino) return px == 16 ? (const lgfx::IFont *)&fonts::efontCN_16 : px == 12 ? (const lgfx::IFont *)&fonts::efontCN_12 : (const lgfx::IFont *)&fonts::efontCN_14;
  return px == 16 ? (const lgfx::IFont *)&fonts::efontJA_16 : px == 12 ? (const lgfx::IFont *)&fonts::efontJA_12 : (const lgfx::IFont *)&fonts::efontJA_14;
}

// Pinta un texto que mezcla latín y chino: cada trozo con la letra que tiene sus
// caracteres (la china no tiene ñ ni tildes; la latina no tiene chino simplificado).
// 'datum' como en LovyanGFX (top_left, middle_center...).
void texto_mixto(LGFX_Sprite &g, const String &t, int x, int y, const lgfx::IFont *tam, textdatum_t datum) {
  // trocear en tramos chino / no chino
  String tramos[12];
  bool es_chino[12];
  int n = 0, i = 0;
  while (i < (int)t.length() && n < 12) {
    bool ch = (uint8_t)t[i] >= 0xE3;
    int j = i;
    while (j < (int)t.length()) {
      uint8_t b = t[j];
      if (((b >= 0xE3) != ch) && (b & 0xC0) != 0x80) break;
      j++;
    }
    tramos[n] = t.substring(i, j);
    es_chino[n++] = ch;
    i = j;
  }
  int ancho = 0;
  for (int k = 0; k < n; k++) { g.setFont(letra_para(es_chino[k] ? "中" : "a", tam)); ancho += g.textWidth(tramos[k]); }
  int h = datum & 3;                               // 0 izquierda, 1 centro, 2 derecha
  int xx = h == 1 ? x - ancho / 2 : h == 2 ? x - ancho : x;
  g.setTextDatum((textdatum_t)(datum & ~3));       // la misma altura, alineado a la izquierda
  for (int k = 0; k < n; k++) {
    g.setFont(letra_para(es_chino[k] ? "中" : "a", tam));
    g.drawString(tramos[k], xx, y);
    xx += g.textWidth(tramos[k]);
  }
  g.setTextDatum(datum);
}

void letras_para(Idioma i) {
  F_TXT = i == ZH ? (const lgfx::IFont *)&fonts::efontCN_14 : (const lgfx::IFont *)&fonts::efontJA_14;
  F_PEQ = i == ZH ? (const lgfx::IFont *)&fonts::efontCN_12 : (const lgfx::IFont *)&fonts::efontJA_12;
  F_GRANDE = i == ZH ? (const lgfx::IFont *)&fonts::efontCN_16 : (const lgfx::IFont *)&fonts::efontJA_16;
}

const int L = 4;   // tamaño del píxel de la escena

inline int rejilla(float v, int p = L) { return (int)lroundf(v / p); }   // px -> casilla

enum Ojos : uint8_t { OJOS_NORMAL, OJOS_CERRADOS, OJOS_FELICES, OJOS_GRANDES, OJOS_CANSADOS };
// IZQ / DER: solo apoya los pies de ese lado (el otro lado, levantado)
enum Piernas : uint8_t { PIERNAS_QUIETAS, PIERNAS_A, PIERNAS_B, PIERNAS_SENTADO, PIERNAS_IZQ, PIERNAS_DER };
// En pixel art no se escala: se cambia de fotograma
enum Forma : uint8_t { FORMA_NORMAL, FORMA_APLASTADO, FORMA_ESTIRADO };

struct Pose {
  float x = 120;          // centro (px de pantalla)
  float suelo = 216;      // dónde están los pies
  float alto = 0;         // cuánto ha saltado
  int8_t mirar = 0;       // -1 izquierda, 0 centro, 1 derecha
  Ojos ojos = OJOS_NORMAL;
  Piernas piernas = PIERNAS_QUIETAS;
  int8_t brazo_i = 0, brazo_d = 0;   // 0 normal, 1 arriba, -1 abajo
  Forma forma = FORMA_NORMAL;
  bool respira = false;   // un píxel más bajito (respirar)
  uint16_t color = CLAWD;
  bool cinta = false;     // cinta de concentración (pomodoro)
  int p = L;              // tamaño del píxel (L en la escena, 1 en la cabecera de la terminal)
  bool guino = false;     // ojo derecho cerrado
  int16_t giro = -1;      // -1 = de frente; 0..359 = girando (90 de canto, 180 de espaldas)
  int sombra = 20;        // ancho de la sombra en píxeles de la rejilla
  bool hundir = false;    // no dibujar por debajo del suelo (para "salir" del suelo)
  int recorte_izq = -1;   // no dibujar a la izquierda de esta x (px): para "asomarse"
};

// Dibuja a Clawd y devuelve la Y (px) de lo alto de su cabeza
int dibujar_clawd(LGFX_Sprite &g, const Pose &po) {
  const int P = po.p;
  int W = 24, H = 16;
  if (po.forma == FORMA_APLASTADO) { W = 26; H = 14; }
  if (po.forma == FORMA_ESTIRADO)  { W = 22; H = 18; }
  if (po.respira) H -= 1;
  const int piernas = po.piernas == PIERNAS_SENTADO ? 0 : 4;
  const int cx = rejilla(po.x, P);
  const int suelo = rejilla(po.suelo, P);
  const int base = rejilla(po.suelo - po.alto, P);   // fila debajo de los pies
  const int arriba = base - piernas - H;
  // girando: el cuerpo se estrecha (coseno) y de espaldas no se ven los ojos
  float cg = 1, sg = 0;
  uint16_t c_cuerpo = po.color;
  if (po.giro >= 0) {
    cg = cosf(po.giro * PI / 180);
    sg = sinf(po.giro * PI / 180);
    W = 2 * max(4, (int)lroundf(12 * fabsf(cg)));
    if (cg < 0) c_cuerpo = C565(186, 98, 70);   // la espalda, un poco más oscura
  }
  const int izq = cx - W / 2;
  const uint16_t c = c_cuerpo;
  auto px = [&](int x, int y, int w, int h, uint16_t col) { g.fillRect(x * P, y * P, w * P, h * P, col); };

  // sombra: una fila de píxeles que se encoge al saltar
  if (P >= 2 && po.sombra > 0) {
    int ancho = max(4, po.sombra - 2 * (int)(max(0.0f, po.alto) / (P * 4)));
    px(cx - ancho / 2, suelo, ancho, 1, SOMBRA);
  }
  // recortes: lo que quede bajo el suelo o tras la "pared" no se ve
  if (po.hundir || po.recorte_izq >= 0) {
    int x0 = max(0, po.recorte_izq);
    g.setClipRect(x0, 0, 240 - x0, po.hundir ? suelo * P : 320);
  }


  // piernas
  if (piernas) {
    const int xs[4] = {-12, -8, 6, 10};  // desde el centro: en las esquinas, como el original
    for (int i = 0; i < 4; i++) {
      bool levantada = (po.piernas == PIERNAS_A && (i == 0 || i == 2)) ||
                       (po.piernas == PIERNAS_B && (i == 1 || i == 3)) ||
                       (po.piernas == PIERNAS_IZQ && i >= 2) ||
                       (po.piernas == PIERNAS_DER && i < 2);
      int lx = (int)lroundf((xs[i] + 1) * fabsf(cg)) - 1;
      if (po.forma == FORMA_APLASTADO) lx += xs[i] < 0 ? -1 : 1;
      if (po.forma == FORMA_ESTIRADO)  lx += xs[i] < 0 ? 1 : -1;
      px(cx + lx, base - 4, 2, levantada ? 2 : 4, c);
    }
  }

  // cuerpo
  px(izq, arriba, W, H, c);

  // brazos
  const int ay = arriba + H / 2;
  // brazo arriba como en Claude Code ("▗▟"): el hombro sube pegado al cuerpo
  // y la pinza queda arriba y hacia fuera
  auto brazo = [&](int x_hombro, int x_pinza, int8_t estado) {
    if (estado == 1) {
      px(x_hombro, ay - 4, 2, 8, c);
      px(x_pinza, ay - 4, 2, 4, c);
    } else {
      int x = min(x_hombro, x_pinza);
      px(x, estado == -1 ? ay + 2 : ay, 4, 4, c);
    }
  };
  if (fabsf(cg) > 0.3f) {          // de canto, los brazos quedan ocultos
    brazo(izq - 2, izq - 4, po.brazo_i);
    brazo(izq + W, izq + W + 2, po.brazo_d);
  }

  // cinta del pomodoro, con su nudo
  if (po.cinta) {
    px(izq, arriba + 1, W, 2, ROJO);
    px(izq + W, arriba, 2, 1, ROJO);
    px(izq + W, arriba + 3, 3, 1, ROJO);
  }

  // ojos
  const int ey = arriba + H / 4;
  for (int lado = 0; lado < 2; lado++) {
    int ex = cx + (lado == 0 ? -8 : 6) + po.mirar * 2;
    if (po.giro >= 0) {               // al girar, los ojos se van hacia el lado al que mira
      if (cg < 0.15f) break;          // de espaldas o de canto: sin ojos
      float d = (lado == 0 ? -7 : 7) * cg + sg * 5;
      ex = cx + (int)lroundf(d) - 1;
      if (ex < izq + 1 || ex + 2 > izq + W - 1) continue;
    }
    Ojos o = (po.guino && lado == 1) ? OJOS_CERRADOS : po.ojos;
    switch (o) {
      case OJOS_NORMAL:   px(ex, ey, 2, 4, OJO); break;
      case OJOS_CERRADOS: px(ex - 1, ey + 2, 4, 1, OJO); break;
      case OJOS_CANSADOS: px(ex, ey + 2, 2, 2, OJO); break;
      case OJOS_FELICES:  // ^
        px(ex - 1, ey + 2, 1, 1, OJO);
        px(ex, ey + 1, 2, 1, OJO);
        px(ex + 2, ey + 2, 1, 1, OJO);
        break;
      case OJOS_GRANDES:            // sorpresa: ojos más grandes, sin pupila (como el original)
        px(ex - 1, ey - 1, 4, 5, OJO);
        break;
    }
  }
  if (po.hundir || po.recorte_izq >= 0) g.clearClipRect();
  return arriba * P;
}

// ---------- Dibujos pequeños en pixel art ----------
// Cada 'X' es un píxel de tamaño p, empezando en (x, y) en px de pantalla
// (la posición se redondea a la rejilla de p).
void patron(LGFX_Sprite &g, const char *const *filas, int nf, int x, int y, int p, uint16_t col) {
  x = rejilla(x, p) * p;
  y = rejilla(y, p) * p;
  for (int f = 0; f < nf; f++)
    for (int c = 0; filas[f][c]; c++)
      if (filas[f][c] == 'X') g.fillRect(x + c * p, y + f * p, p, p, col);
}
const char *const PAT_CORAZON[] = {".X.X.", "XXXXX", ".XXX.", "..X.."};
const char *const PAT_NOTA[]    = {".XXX", ".X.X", ".X..", "XX..", "XX.."};
const char *const PAT_GOTA[]    = {".X.", "XXX", "XXX", ".X."};
const char *const PAT_ZETA_P[]  = {"XXX", ".X.", "XXX"};
const char *const PAT_ZETA_G[]  = {"XXXX", "..X.", ".X..", "XXXX"};
const char *const PAT_PUNTO[]   = {"X"};
const char *const PAT_RAYO[]    = {"..X", ".X.", "XXX", ".X.", "X.."};

// Iconos de la barra de arriba (píxel de 2)
const char *const ICO_ALTAVOZ[]    = {"...X...X.", "..XX.X..X", "XXXX..X.X", "XXXX..X.X", "XXXX..X.X", "..XX.X..X", "...X...X."};
const char *const ICO_ALTAVOZ_NO[] = {"...X.....", "..XX.....", "XXXX.X.X.", "XXXX..X..", "XXXX.X.X.", "..XX.....", "...X....."};
const char *const ICO_MICRO[]      = {"..XXX..", "..XXX..", "X.XXX.X", "X.XXX.X", ".X...X.", "..XXX..", "...X...", "..XXX.."};
const char *const ICO_TACHON[]     = {"X......", ".X.....", "..X....", "...X...", "....X..", ".....X.", "......X", "......X"};

// ---------- Fuente pixel art de 3x5 (para bocadillos y carteles) ----------
struct Glifo { char c; const char *f[5]; };
const Glifo FUENTE[] = {
  {'A', {".X.", "X.X", "XXX", "X.X", "X.X"}}, {'B', {"XX.", "X.X", "XX.", "X.X", "XX."}},
  {'C', {".XX", "X..", "X..", "X..", ".XX"}}, {'D', {"XX.", "X.X", "X.X", "X.X", "XX."}},
  {'E', {"XXX", "X..", "XX.", "X..", "XXX"}}, {'F', {"XXX", "X..", "XX.", "X..", "X.."}},
  {'G', {".XX", "X..", "X.X", "X.X", ".XX"}}, {'H', {"X.X", "X.X", "XXX", "X.X", "X.X"}},
  {'I', {"XXX", ".X.", ".X.", ".X.", "XXX"}}, {'J', {"..X", "..X", "..X", "X.X", ".X."}},
  {'K', {"X.X", "X.X", "XX.", "X.X", "X.X"}}, {'L', {"X..", "X..", "X..", "X..", "XXX"}},
  {'M', {"X...X", "XX.XX", "X.X.X", "X...X", "X...X"}}, {'N', {"X..X", "XX.X", "X.XX", "X..X", "X..X"}},
  {'O', {".X.", "X.X", "X.X", "X.X", ".X."}}, {'P', {"XX.", "X.X", "XX.", "X..", "X.."}},
  {'Q', {".X.", "X.X", "X.X", "XX.", ".XX"}}, {'R', {"XX.", "X.X", "XX.", "X.X", "X.X"}},
  {'S', {".XX", "X..", ".X.", "..X", "XX."}}, {'T', {"XXX", ".X.", ".X.", ".X.", ".X."}},
  {'U', {"X.X", "X.X", "X.X", "X.X", "XXX"}}, {'V', {"X.X", "X.X", "X.X", "X.X", ".X."}},
  {'W', {"X...X", "X...X", "X.X.X", "XX.XX", "X...X"}}, {'X', {"X.X", "X.X", ".X.", "X.X", "X.X"}},
  {'Y', {"X.X", "X.X", ".X.", ".X.", ".X."}}, {'Z', {"XXX", "..X", ".X.", "X..", "XXX"}},
  {'0', {"XXX", "X.X", "X.X", "X.X", "XXX"}}, {'1', {".X.", "XX.", ".X.", ".X.", "XXX"}},
  {'2', {"XX.", "..X", ".X.", "X..", "XXX"}}, {'3', {"XX.", "..X", ".X.", "..X", "XX."}},
  {'4', {"X.X", "X.X", "XXX", "..X", "..X"}}, {'5', {"XXX", "X..", "XX.", "..X", "XX."}},
  {'6', {".XX", "X..", "XXX", "X.X", "XXX"}}, {'7', {"XXX", "..X", ".X.", ".X.", ".X."}},
  {'8', {"XXX", "X.X", "XXX", "X.X", "XXX"}}, {'9', {"XXX", "X.X", "XXX", "..X", "XX."}},
  {'!', {"X", "X", "X", ".", "X"}},           {'?', {"XX.", "..X", ".X.", "...", ".X."}},
  {'.', {".", ".", ".", ".", "X"}},           {',', {".", ".", ".", "X", "X"}},
  {':', {".", "X", ".", "X", "."}},           {'\'', {"X", "X", ".", ".", "."}},
  {'-', {"...", "...", "XXX", "...", "..."}}, {'/', {"..X", "..X", ".X.", "X..", "X.."}},
  {'(', {".X", "X.", "X.", "X.", ".X"}},      {')', {"X.", ".X", ".X", ".X", "X."}},
  {' ', {"..", "..", "..", "..", ".."}},
};

const Glifo *glifo(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;   // solo mayúsculas
  for (const Glifo &gl : FUENTE)
    if (gl.c == c) return &gl;
  return nullptr;
}

// ancho del texto en píxeles de la rejilla (de tamaño p)
int ancho_pixel(const char *t, int p = L) {
  if (no_ascii(t)) {                     // se pintará con la letra normal (efont: 8 px por columna)
    int px = columnas_de(t) * (p >= 3 ? 8 : 7);
    return (px + p - 1) / p;
  }
  int w = 0;
  for (; *t; t++) {
    const Glifo *gl = glifo(*t);
    if (gl) w += strlen(gl->f[0]) + 1;
  }
  return w > 0 ? w - 1 : 0;
}

// escribe texto con la fuente pixel; (x, y) en casillas de tamaño p.
// Si el texto no es ASCII (chino, tildes...), con la letra normal en su lugar.
void texto_pixel(LGFX_Sprite &g, const char *t, int x, int y, int p, uint16_t col) {
  if (no_ascii(t)) {
    g.setFont(p >= 3 ? F_GRANDE : F_TXT);
    g.setTextDatum(top_left);
    g.setTextColor(col);
    g.drawString(t, x * p, y * p + (5 * p - g.fontHeight()) / 2);
    return;
  }
  for (; *t; t++) {
    const Glifo *gl = glifo(*t);
    if (!gl) continue;
    int w = strlen(gl->f[0]);
    for (int f = 0; f < 5; f++)
      for (int c = 0; c < w; c++)
        if (gl->f[f][c] == 'X') g.fillRect((x + c) * p, (y + f) * p, p, p, col);
    x += w + 1;
  }
}

// caja con las esquinas "mordidas" (el redondeo del pixel art)
void caja_pixel(LGFX_Sprite &g, int x, int y, int w, int h, int p, uint16_t col) {
  g.fillRect((x + 1) * p, y * p, (w - 2) * p, h * p, col);
  g.fillRect(x * p, (y + 1) * p, w * p, (h - 2) * p, col);
}

// ---------- Partículas: corazones, notas, Zzz, gotas, confeti ----------
enum TipoPart : uint8_t { P_CORAZON, P_NOTA, P_ZETA, P_GOTA, P_CONFETI };
struct Part {
  float x, y, vx, vy;
  int16_t vida, vida_max;
  TipoPart tipo;
  uint16_t color;
  bool activa;
};
Part parts[48];

void soltar(TipoPart t, float x, float y, float vx, float vy, int vida, uint16_t col) {
  for (auto &p : parts) {
    if (p.activa) continue;
    p = {x, y, vx, vy, (int16_t)vida, (int16_t)vida, t, col, true};
    return;
  }
}

void dibujar_parts(LGFX_Sprite &g) {
  for (auto &p : parts) {
    if (!p.activa) continue;
    p.x += p.vx;
    p.y += p.vy;
    if (p.tipo == P_CONFETI || p.tipo == P_GOTA) p.vy += 0.25f;   // gravedad
    if (p.tipo == P_ZETA || p.tipo == P_NOTA) p.x += sinf(p.vida * 0.15f) * 0.6f;
    if (--p.vida <= 0 || p.y > 248 || p.y < 32) { p.activa = false; continue; }
    int x = (int)p.x, y = (int)p.y;
    switch (p.tipo) {
      case P_CORAZON: patron(g, PAT_CORAZON, 4, x - 10, y - 8, L, p.color); break;
      case P_NOTA:    patron(g, PAT_NOTA, 5, x - 8, y - 10, L, p.color); break;
      case P_GOTA:    patron(g, PAT_GOTA, 4, x - 6, y - 8, L, p.color); break;
      case P_CONFETI: patron(g, PAT_PUNTO, 1, x, y, L, p.color); break;
      case P_ZETA: {
        bool grande = p.vida < p.vida_max * 0.6f;
        if (grande) patron(g, PAT_ZETA_G, 4, x - 8, y - 8, L, p.color);
        else        patron(g, PAT_ZETA_P, 3, x - 6, y - 6, L, p.color);
        break;
      }
    }
  }
}

void soltar_confeti(float x, float y, int n) {
  const uint16_t cols[] = {CLAWD, TXT, TXT2, VERDE};   // identidad + "bien"
  for (int i = 0; i < n; i++)
    soltar(P_CONFETI, x, y, random(-30, 31) / 10.0f, random(-60, -20) / 10.0f, random(30, 60), cols[random(4)]);
}

// ---------- El "spinner" de Claude Code, en píxeles ----------
// Como en la terminal: la estrellita crece y encoge  ·  +  *  +
// La terminal usa los caracteres  ·  ✢  ✳  ✶  ✻  ✽  y vuelve hacia atrás.
const char *const SPIN_0[] = {".....", ".....", "..X..", ".....", "....."};   // ·
const char *const SPIN_1[] = {"..X..", ".....", "X.X.X", ".....", "..X.."};   // ✢
const char *const SPIN_2[] = {"X.X.X", ".XXX.", "XXXXX", ".XXX.", "X.X.X"};   // ✳
const char *const SPIN_3[] = {"..X..", ".XXX.", "XXXXX", ".XXX.", "..X.."};   // ✶
const char *const SPIN_4[] = {"X.X.X", ".XXX.", "XX.XX", ".XXX.", "X.X.X"};   // ✻
const char *const SPIN_5[] = {"X...X", ".X.X.", "..X..", ".X.X.", "X...X"};   // ✽ (a 5x5 parecía una "#")
// (cx, cy) = centro en px de pantalla; p = tamaño del píxel
void dibujar_spinner(LGFX_Sprite &g, int cx, int cy, uint32_t t, uint16_t col, int p = L) {
  const char *const *fr[] = {SPIN_0, SPIN_1, SPIN_3, SPIN_2, SPIN_4, SPIN_2, SPIN_3, SPIN_1, SPIN_0, SPIN_0};
  patron(g, fr[(t / 120) % 10], 5, cx - 2 * p, cy - 2 * p, p, col);
}

// Las nubecitas de las animaciones de Claude Code:  ·  y  ~
const char *const POOF_PUNTO[] = {"X"};
const char *const POOF_OLA[]   = {".XX..", "X..XX"};

// ---------- Bocadillo con texto pixel ----------
// cx = centro (px), y_abajo = dónde acaba el pico (px)
void bocadillo(LGFX_Sprite &g, int cx, int y_abajo, const char *txt, uint16_t fondo, uint16_t tinta) {
  const int max_x = 240 / L;
  int w = ancho_pixel(txt) + 4, h = 9;
  int c = rejilla(cx);
  int x = constrain(c - w / 2, 1, max_x - 1 - w);
  int y = rejilla(y_abajo) - h - 2;
  int pico = constrain(c, x + 2, x + w - 3);
  caja_pixel(g, x, y, w, h, L, fondo);
  g.fillRect((pico - 1) * L, (y + h) * L, 3 * L, L, fondo);   // el pico
  g.fillRect(pico * L, (y + h + 1) * L, L, L, fondo);
  texto_pixel(g, txt, x + 2, y + 2, L, tinta);
}
