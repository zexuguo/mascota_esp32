#pragma once
// ------------------------------------------------------------
// animaciones.h — las 15 animaciones de Clawd de Claude Code
//
// Sacadas del propio Claude Code (v2.1.332): allí Clawd es arte de texto y
// cada animación es una lista de fotogramas de 60 ms. Aquí están las mismas
// listas, fotograma a fotograma y con los mismos nombres. Cada fotograma dice:
//   - la pose: ojos (abiertos, izquierda, derecha, cerrados, guiño),
//              brazos (abajo, arriba, uno arriba), pies (los dos, izq, der)
//              o "de cara a" un ángulo (para darse la vuelta)
//   - offset: cuántas filas se hunde (+) o se eleva (-). Clawd mide 3 filas.
//   - x: cuántas columnas se desplaza (Clawd mide 9 columnas)
//   - poof: nubecitas a los lados ("·" o "~")
//   - sombra: ancha o estrecha (para el giro de "coin-hop")
//
// En Claude Code:  al arrancar hace una al azar (la "entrada"),
//                  y al hacer clic en él, otra al azar.
// ------------------------------------------------------------
#include <vector>
#include "clawd.h"

enum OjosCC : uint8_t { CC_ABIERTOS, CC_IZQ, CC_DER, CC_CERRADOS, CC_GUINO };
enum BrazosCC : uint8_t { CC_ABAJO, CC_ARRIBA, CC_UNO_ARRIBA };
enum PiesCC : uint8_t { CC_AMBOS, CC_PIE_IZQ, CC_PIE_DER };
enum PoofCC : uint8_t { CC_SIN_POOF, CC_PUNTO, CC_OLA };
enum SombraCC : uint8_t { CC_SOMBRA_NORMAL, CC_SOMBRA_ANCHA, CC_SOMBRA_ESTRECHA };

struct Fotograma {
  OjosCC ojos = CC_ABIERTOS;
  BrazosCC brazos = CC_ABAJO;
  PiesCC pies = CC_AMBOS;
  int8_t offset = 0;
  int8_t x = 0;
  PoofCC poof = CC_SIN_POOF;
  SombraCC sombra = CC_SOMBRA_NORMAL;
  int16_t giro = -1;
};

const uint32_t MS_FOTOGRAMA = 60;   // como en Claude Code
const int FILA_PX = 24;             // 1 fila de la terminal = 1/3 de Clawd
const int COLUMNA_PX = 16;          // 1 columna = 1/9 de Clawd (más o menos)

// ---- las poses con nombre ----
Fotograma pose(const char *nombre) {
  Fotograma f;
  if (!strcmp(nombre, "arms-up")) f.brazos = CC_ARRIBA;
  else if (!strcmp(nombre, "look-left")) f.ojos = CC_IZQ;
  else if (!strcmp(nombre, "look-right")) f.ojos = CC_DER;
  return f;   // "default": ojos abiertos, brazos abajo, los dos pies
}
Fotograma m(OjosCC ojos, BrazosCC brazos = CC_ABAJO, PiesCC pies = CC_AMBOS) {
  Fotograma f;
  f.ojos = ojos; f.brazos = brazos; f.pies = pies;
  return f;
}
// los ángulos de los fotogramas de giro ("right-30", "back-150"...)
int16_t angulo(const char *n) {
  struct { const char *n; int16_t a; } t[] = {
    {"right-12", 12}, {"right-30", 30}, {"right-55", 55}, {"right-75", 75}, {"edge", 90},
    {"back-105", 105}, {"back-125", 125}, {"back-150", 150}, {"back", 180},
    {"left-75", 285}, {"left-55", 305}, {"left-30", 330}, {"left-12", 348}};
  for (auto &e : t) if (!strcmp(e.n, n)) return e.a;
  return -1;
}

// ---- ayudantes, igual que en el código de Claude Code ----
typedef std::vector<Fotograma> Secuencia;

// t(pose, n, offset, x, poof, sombra): repetir un fotograma n veces
Secuencia t(Fotograma f, int n, int8_t offset = 0, int8_t x = 0, PoofCC poof = CC_SIN_POOF,
            SombraCC sombra = CC_SOMBRA_NORMAL) {
  f.offset = offset; f.x = x; f.poof = poof; f.sombra = sombra;
  return Secuencia(n, f);
}
Secuencia t(const char *nombre, int n, int8_t offset = 0, int8_t x = 0, PoofCC poof = CC_SIN_POOF,
            SombraCC sombra = CC_SOMBRA_NORMAL) {
  return t(pose(nombre), n, offset, x, poof, sombra);
}
// v(giros, sombra): un fotograma por cada ángulo
Secuencia v(std::initializer_list<const char *> giros, SombraCC sombra = CC_SOMBRA_NORMAL) {
  Secuencia s;
  for (const char *g : giros) {
    Fotograma f;
    f.giro = angulo(g);
    f.sombra = sombra;
    s.push_back(f);
  }
  return s;
}
// juntar varias secuencias en una
Secuencia operator+(Secuencia a, const Secuencia &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}
// A(): agacharse soltando nubecitas
Secuencia A() { return t("default", 1, 1, 0, CC_PUNTO) + t("default", 1, 1, 0, CC_OLA); }
// h: quieto
Secuencia h() { return t("default", 1); }
// V: parpadeo al final
Secuencia V() { return t(m(CC_CERRADOS), 1) + h(); }

const char *const NOMBRES_ANIM[] = {"jump", "look", "celebrate", "skip", "spin", "peekaboo", "drop",
                                    "waddle", "peek", "wink", "boop", "tap", "sneeze", "turn", "coin-hop"};
const int N_ANIM = 15;
enum { A_JUMP, A_LOOK, A_CELEBRATE, A_SKIP, A_SPIN, A_PEEKABOO, A_DROP, A_WADDLE, A_PEEK, A_WINK,
       A_BOOP, A_TAP, A_SNEEZE, A_TURN, A_COIN_HOP };
Secuencia ANIM[N_ANIM];

void construir_animaciones() {
  Secuencia ge = A() + t("arms-up", 3) + t("default", 1) + A() + t("arms-up", 3) + t("default", 1);
  ANIM[A_JUMP] = ge;
  ANIM[A_LOOK] = t("look-right", 5) + t("look-left", 5) + t("default", 1);
  ANIM[A_CELEBRATE] = ge + t("default", 3, 1);
  ANIM[A_SPIN] = t("look-left", 2) + t("look-right", 2) + t("look-left", 2) + t("arms-up", 3) + t("default", 1);
  ANIM[A_SKIP] = t("default", 1, 1, -9) + t("arms-up", 2, 0, -6) + t("default", 1, 0, -6) +
                 t("default", 1, 1, -6) + t("arms-up", 2, 0, -3) + t("default", 1, 0, -3) +
                 t("default", 1, 1, -3) + t("arms-up", 2) + A() + t("default", 1);
  ANIM[A_PEEKABOO] = t("default", 1, 3) + t("default", 3, 2) + t("look-right", 3, 2) + t("look-left", 3, 2) +
                     t("default", 2, 2) + t("default", 1, 1) + t("arms-up", 4) + t("default", 2) + V();
  ANIM[A_DROP] = t("arms-up", 1, -3) + t("arms-up", 2, -2) + t("arms-up", 2, -1) + t("arms-up", 1) + A() +
                 t("default", 3) + V();
  Secuencia waddle = t("look-right", 1, 0, -9);
  for (int r = -6; r <= -1; r++) waddle = waddle + t(m(CC_DER, CC_ABAJO, r % 2 == 0 ? CC_PIE_IZQ : CC_PIE_DER), 2, 0, r);
  ANIM[A_WADDLE] = waddle + t("look-right", 2) + t("default", 2) + V();
  ANIM[A_PEEK] = t("look-right", 1, 0, -9) + t("look-right", 5, 0, -6) + t("look-right", 3, 0, -9) +
                 t("look-right", 4, 0, -5) + t("arms-up", 2, 0, -3) + t("arms-up", 2) + A() + h();
  ANIM[A_WINK] = t(m(CC_GUINO), 5) + h();
  ANIM[A_BOOP] = t(m(CC_CERRADOS), 1, 1, 0, CC_PUNTO) + t(m(CC_CERRADOS), 2, 1, 0, CC_OLA) + t("look-left", 4) +
                 t("default", 2) + V();
  ANIM[A_TAP] = t(m(CC_ABIERTOS, CC_ABAJO, CC_PIE_IZQ), 2) + t(m(CC_ABIERTOS, CC_ABAJO, CC_PIE_DER), 2) +
                t(m(CC_ABIERTOS, CC_ABAJO, CC_PIE_IZQ), 2) + t(m(CC_ABIERTOS, CC_ABAJO, CC_PIE_DER), 2) +
                t(m(CC_ABIERTOS, CC_ABAJO, CC_PIE_IZQ), 1) + t(m(CC_ABIERTOS, CC_ABAJO, CC_PIE_DER), 1) +
                t("arms-up", 3) + h();
  ANIM[A_SNEEZE] = t(m(CC_CERRADOS, CC_ARRIBA), 4) + t(m(CC_CERRADOS), 2, 1, 0, CC_OLA) + t(m(CC_CERRADOS), 2) +
                   t("default", 2) + V();
  ANIM[A_TURN] = v({"right-12", "right-30", "right-55", "right-75", "edge"}) +
                 v({"back-105", "back-125", "back-150", "back", "back"}) +
                 v({"back-150", "back-125", "back-105", "edge"}) +
                 v({"left-75", "left-55", "left-30", "left-12"}) + h();
  ANIM[A_COIN_HOP] = t("default", 2, 1) + t("arms-up", 1, 0, 0, CC_SIN_POOF, CC_SOMBRA_ANCHA) +
                     v({"right-55", "edge", "back-125", "back"}, CC_SOMBRA_ESTRECHA) +
                     v({"back-125", "edge", "left-55"}, CC_SOMBRA_ESTRECHA) +
                     t("arms-up", 1, 0, 0, CC_SIN_POOF, CC_SOMBRA_ANCHA) + A() + h();
}

// Las que valen para el clic: las que no entran desde fuera (x = 0 al empezar)
bool anim_para_clic(int i) { return ANIM[i][0].x == 0; }

int anim_por_nombre(const char *n) {
  for (int i = 0; i < N_ANIM; i++) if (!strcmp(NOMBRES_ANIM[i], n)) return i;
  return -1;
}

// ---- reproductor ----
int anim_actual = -1;
uint32_t anim_ini = 0;

void animar(int i, uint32_t ahora) {
  if (i < 0 || i >= N_ANIM) return;
  anim_actual = i;
  anim_ini = ahora;
}

int anim_al_azar(bool solo_clic) {
  int i;
  do { i = random(N_ANIM); } while (solo_clic && !anim_para_clic(i));
  return i;
}

// ¿Hay animación? Si sí, rellena el fotograma de ahora.
bool anim_fotograma(uint32_t ahora, Fotograma &f) {
  if (anim_actual < 0) return false;
  uint32_t k = (ahora - anim_ini) / MS_FOTOGRAMA;
  if (k >= ANIM[anim_actual].size()) { anim_actual = -1; return false; }
  f = ANIM[anim_actual][k];
  return true;
}

// Pasa un fotograma de Claude Code a la pose de nuestro Clawd
void aplicar_fotograma(const Fotograma &f, Pose &p, int x_caja) {
  p.mirar = 0;
  p.ojos = OJOS_NORMAL;
  p.guino = false;
  switch (f.ojos) {
    case CC_IZQ:      p.mirar = -1; break;
    case CC_DER:      p.mirar = 1; break;
    case CC_CERRADOS: p.ojos = OJOS_CERRADOS; break;
    case CC_GUINO:    p.guino = true; break;
    default: break;
  }
  p.brazo_i = f.brazos == CC_ARRIBA ? 1 : 0;
  p.brazo_d = f.brazos == CC_ABAJO ? 0 : 1;          // "uno arriba" = el derecho
  p.piernas = f.pies == CC_PIE_IZQ ? PIERNAS_IZQ : (f.pies == CC_PIE_DER ? PIERNAS_DER : PIERNAS_QUIETAS);
  p.alto = -f.offset * FILA_PX;                       // + se hunde, - se eleva
  p.x += f.x * COLUMNA_PX;
  p.giro = f.giro;
  p.forma = FORMA_NORMAL;
  p.respira = false;
  p.sombra = f.sombra == CC_SOMBRA_ANCHA ? 22 : (f.sombra == CC_SOMBRA_ESTRECHA ? 6 : 20);
  p.hundir = f.offset > 0;
  if (f.x < 0) p.recorte_izq = x_caja;               // sale de detrás de una "pared"
}

// Las nubecitas: a los dos lados de Clawd, a ras de suelo
void dibujar_poof(LGFX_Sprite &g, const Fotograma &f, int cx, int suelo) {
  if (f.poof == CC_SIN_POOF || f.offset <= 0) return;
  const char *const *pat = f.poof == CC_PUNTO ? POOF_PUNTO : POOF_OLA;
  int filas = f.poof == CC_PUNTO ? 1 : 2;
  int ancho = f.poof == CC_PUNTO ? 1 : 5;
  patron(g, pat, filas, cx - 68 - ancho * L / 2, suelo - 3 * L, L, GRIS);
  patron(g, pat, filas, cx + 68 - ancho * L / 2, suelo - 3 * L, L, GRIS);
}
