#pragma once
// ------------------------------------------------------------
// ajedrez.h — la revisión de partidas de chess.com en la placa
//
// El PC (ajedrez.py) analiza la partida en la nube y manda:
//   {"aj": {...}}      el resumen (precisión, cuántas jugadas de cada tipo, letras de cada jugada)
//   {"aj_ply": {...}}  una jugada concreta (posición, jugada, mejor jugada, evaluación)
// La placa pide las jugadas de una en una ({"ev":"aj:ply:N"}): así no hace falta memoria.
//
// Clases (como chess.com): K teoría, R brillante, B mejor, E excelente, G buena,
// I imprecisión, M error, X error grave, F forzada. Cada una con su color de chess.com.
// ------------------------------------------------------------

// ---- estado ----
uint8_t aj_estado = 0;          // 0 sin partida, 1 analizando, 2 lista
float aj_progreso = 0;
String aj_rival, aj_res, aj_ritmo, aj_clases;
char aj_color = 'w';            // tu color
float aj_acc[2] = {0, 0};       // tu precisión, la del rival
int aj_n = 0;                   // jugadas (medias jugadas) de la partida
int aj_vista = 0;               // 0 resumen, 1 tablero
int aj_i = 0;                   // jugada que se ve (0 = posición inicial)
bool aj_errores = false;        // navegar solo por tus fallos
uint32_t aj_pedido = 0;         // cuándo se pidió la jugada (para no pedirla 20 veces)
// la jugada que se está viendo
String aj_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR";
String aj_san, aj_mejor, aj_eval, aj_uci, aj_mejor_uci;
char aj_clase = 0;
int aj_num = 0;
bool aj_blancas = true;
int aj_ply_cargado = -1;

const char AJ_ORDEN[] = "RBEGKIMXF";     // orden de la tabla del resumen
uint16_t color_clase(char c) {
  switch (c) {
    case 'R': return C565(27, 172, 166);   // brillante (turquesa)
    case 'B': return C565(150, 188, 75);   // mejor (verde)
    case 'E': return C565(116, 176, 72);   // excelente
    case 'G': return C565(149, 175, 138);  // buena
    case 'K': return C565(168, 136, 101);  // teoría (marrón)
    case 'I': return C565(247, 192, 69);   // imprecisión (amarillo)
    case 'M': return C565(255, 164, 89);   // error (naranja)
    case 'X': return C565(250, 65, 45);    // error grave (rojo)
    default:  return TXT3;                 // forzada
  }
}
const char *nombre_clase(char c) {
  switch (c) {
    case 'R': return T("Brillante");
    case 'B': return T("Mejor");
    case 'E': return T("Excelente");
    case 'G': return T("Buena");
    case 'K': return T("Teoría");
    case 'I': return T("Imprecisión");
    case 'M': return T("Error");
    case 'X': return T("Error grave");
    case 'F': return T("Forzada");
    default:  return "";
  }
}
const char *simbolo_clase(char c) {
  switch (c) {
    case 'R': return "!!"; case 'B': return "*"; case 'E': return "!"; case 'G': return "+";
    case 'K': return "="; case 'I': return "?!"; case 'M': return "?"; case 'X': return "??";
    default:  return ">";
  }
}
bool es_tu_jugada(int ply) { return ((ply - 1) % 2 == 0) == (aj_color == 'w'); }   // ply 1 = 1.ª jugada de blancas
bool es_fallo(char c) { return c == 'I' || c == 'M' || c == 'X'; }

// ---- piezas en pixel art (12 x 12, píxel de 2 = 24 px) ----
const char *const PZ_P[] = {"............", "............", ".....XX.....", "....XXXX....", "....XXXX....", ".....XX.....",
                            "....XXXX....", ".....XX.....", "....XXXX....", "...XXXXXX...", "..XXXXXXXX..", "............"};
const char *const PZ_R[] = {"............", "..XX.XX.XX..", "..XXXXXXXX..", "...XXXXXX...", "...XXXXXX...", "...XXXXXX...",
                            "...XXXXXX...", "...XXXXXX...", "...XXXXXX...", "..XXXXXXXX..", ".XXXXXXXXXX.", "............"};
const char *const PZ_N[] = {"............", ".....XX.....", "....XXXXX...", "...XXXXXXX..", "..XXX.XXXX..", "..XXXXXXXX..",
                            "......XXXX..", ".....XXXXX..", "....XXXXXX..", "...XXXXXXX..", "..XXXXXXXXX.", "............"};
const char *const PZ_B[] = {"............", ".....XX.....", "....XXXX....", "....XX.X....", "...XXX.XX...", "...XXXXXX...",
                            "....XXXX....", ".....XX.....", "....XXXX....", "..XXXXXXXX..", "..XXXXXXXX..", "............"};
const char *const PZ_Q[] = {"............", ".X...XX...X.", ".XX..XX..XX.", ".XXX.XX.XXX.", "..XXXXXXXX..", "..XXXXXXXX..",
                            "...XXXXXX...", "....XXXX....", "...XXXXXX...", "..XXXXXXXX..", ".XXXXXXXXXX.", "............"};
const char *const PZ_K[] = {".....XX.....", "....XXXX....", ".....XX.....", "..XX.XX.XX..", ".XXXXXXXXXX.", ".XXXXXXXXXX.",
                            "..XXXXXXXX..", "...XXXXXX...", "...XXXXXX...", "..XXXXXXXX..", ".XXXXXXXXXX.", "............"};

void dibujar_pieza(char p, int x, int y) {
  const char *const *pat = nullptr;
  switch (tolower(p)) {
    case 'p': pat = PZ_P; break; case 'r': pat = PZ_R; break; case 'n': pat = PZ_N; break;
    case 'b': pat = PZ_B; break; case 'q': pat = PZ_Q; break; case 'k': pat = PZ_K; break;
  }
  if (!pat) return;
  bool blanca = isupper(p);
  uint16_t relleno = blanca ? C565(248, 248, 244) : C565(52, 50, 48);
  uint16_t borde = blanca ? C565(40, 38, 36) : C565(200, 196, 188);
  for (int dx = -1; dx <= 1; dx++)            // el borde: la misma figura un píxel alrededor
    for (int dy = -1; dy <= 1; dy++)
      if (dx || dy) patron(g, pat, 12, x + dx * 2, y + dy * 2, 2, borde);
  patron(g, pat, 12, x, y, 2, relleno);
}

// ---- recibir del PC ----
void aj_recibir_resumen(JsonObject a) {
  aj_estado = String(a["estado"] | "") == "analizando" ? 1 : 2;
  aj_progreso = a["progreso"] | 0.0f;
  if (aj_estado == 1) return;
  aj_rival = a["rival"] | "";
  aj_res = a["res"] | "";
  aj_ritmo = a["ritmo"] | "";
  aj_color = (a["color"] | "w")[0];
  aj_acc[0] = a["acc"][0] | 0.0f;
  aj_acc[1] = a["acc"][1] | 0.0f;
  aj_clases = a["clases"] | "";
  aj_n = aj_clases.length();
  aj_ply_cargado = -1;
  if (a["nuevo"] | 0) {                          // una partida nueva: aviso discreto, sin sonido
    aj_vista = 0;
    notificar(2, T("Partida analizada"), String((int)aj_acc[0]) + "% · vs " + aj_rival);
  }
}

void aj_recibir_ply(JsonObject p) {
  aj_ply_cargado = p["i"] | 0;
  aj_fen = p["fen"] | "";
  aj_san = p["san"] | "";
  aj_mejor = p["mejor"] | "";
  aj_eval = p["eval"] | "";
  aj_uci = p["uci"] | "";
  aj_mejor_uci = p["mejor_uci"] | "";
  aj_clase = (p["clase"] | " ")[0];
  aj_num = p["num"] | 0;
  aj_blancas = p["blancas"] | true;
}

void aj_ir(int i) {
  aj_i = constrain(i, 0, aj_n);
  if (aj_ply_cargado != aj_i) {
    char b[24];
    snprintf(b, sizeof(b), "aj:ply:%d", aj_i);
    pc_enviar(b);
    aj_pedido = ahora;
  }
}

// siguiente / anterior (si "solo errores": solo tus imprecisiones, errores y errores graves)
void aj_paso(int d) {
  int i = aj_i;
  do { i += d; } while (aj_errores && i >= 1 && i <= aj_n && !(es_tu_jugada(i) && es_fallo(aj_clases[i - 1])));
  if (i >= 0 && i <= aj_n) aj_ir(i);
  SONAR(S_TIC);
}

// ---- dibujar ----
void aj_resumen() {
  g.setTextDatum(middle_left);
  g.setFont(F_GRANDE);
  g.setTextColor(TXT);
  g.drawString(T("Revisión de partida"), 14, 42);
  if (aj_estado == 0) {
    g.setFont(F_PEQ);
    g.setTextColor(TXT3);
    g.drawString(T("Juega en chess.com y aparecerá aquí."), 14, 70);
    boton(10, 266, 220, 40, T("Analizar la última"), true);
    return;
  }
  if (aj_estado == 1) {
    g.setFont(F_PEQ);
    g.setTextColor(TXT2);
    g.drawString(T("Analizando en la nube…"), 14, 70);
    barra(14, 86, 212, 4, aj_progreso, VERDE);
    return;
  }
  g.setFont(F_PEQ);
  g.setTextColor(TXT2);
  String res = aj_res == "gana" ? T("Victoria") : aj_res == "pierde" ? T("Derrota") : T("Tablas");
  g.drawString(recortar(res + " · vs " + aj_rival + " · " + aj_ritmo, 212), 14, 60);
  // precisión: tú y el rival, en grande
  char b[12];
  for (int k = 0; k < 2; k++) {
    int x = k == 0 ? 14 : 126;
    tarjeta(x - 4, 72, 104, 46, k == 0 ? SUP2 : SUP);
    g.setFont(F_PEQ);
    g.setTextColor(TXT3);
    g.setTextDatum(top_left);
    g.drawString(k == 0 ? T("Tú") : T("Rival"), x + 4, 76);
    snprintf(b, sizeof(b), "%.1f", aj_acc[k]);
    texto_pixel(g, b, (x + 4) / 4, 92 / 4, 4, k == 0 ? TXT : TXT2);
  }
  // tabla de jugadas por tipo (tú | rival)
  int y = 126;
  for (const char *c = AJ_ORDEN; *c; c++, y += 16) {
    int n[2] = {0, 0};
    for (int i = 0; i < aj_n; i++) if (aj_clases[i] == *c) n[es_tu_jugada(i + 1) ? 0 : 1]++;
    g.fillCircle(22, y + 7, 6, color_clase(*c));
    g.setFont(F_PEQ);
    g.setTextDatum(middle_left);
    g.setTextColor(TXT2);
    g.drawString(nombre_clase(*c), 34, y + 7);
    g.setTextDatum(middle_right);
    g.setTextColor(n[0] ? color_clase(*c) : TXT3);
    g.drawString(String(n[0]), 176, y + 7);
    g.setTextColor(n[1] ? TXT2 : TXT3);
    g.drawString(String(n[1]), 222, y + 7);
  }
  boton(10, 274, 106, 38, T("Mis errores"), false);
  boton(124, 274, 106, 38, T("Revisar"), true);
}

void aj_casilla(int col, int fila, int &x, int &y) {   // fila 0 = la 8.ª (arriba, con blancas abajo)
  bool giro = aj_color == 'b';
  x = (giro ? 7 - col : col) * 30;
  y = BARRA_H + (giro ? 7 - fila : fila) * 30;
}

void aj_tablero() {
  // casillas (colores de chess.com) y la jugada hecha, resaltada
  int de_c = -1, de_f = -1, a_c = -1, a_f = -1;
  if (aj_uci.length() >= 4) {
    de_c = aj_uci[0] - 'a'; de_f = '8' - aj_uci[1];
    a_c = aj_uci[2] - 'a';  a_f = '8' - aj_uci[3];
  }
  for (int f = 0; f < 8; f++)
    for (int c = 0; c < 8; c++) {
      int x, y;
      aj_casilla(c, f, x, y);
      bool clara = (f + c) % 2 == 0;
      bool marcada = (c == de_c && f == de_f) || (c == a_c && f == a_f);
      uint16_t col = clara ? C565(235, 236, 208) : C565(119, 149, 86);
      if (marcada) col = clara ? C565(245, 246, 130) : C565(185, 202, 67);
      g.fillRect(x, y, 30, 30, col);
    }
  // piezas (del FEN)
  int f = 0, c = 0;
  for (size_t k = 0; k < aj_fen.length() && f < 8; k++) {
    char ch = aj_fen[k];
    if (ch == '/') { f++; c = 0; }
    else if (isdigit(ch)) c += ch - '0';
    else { int x, y; aj_casilla(c, f, x, y); dibujar_pieza(ch, x + 3, y + 3); c++; }
  }
  // la flecha de la mejor jugada (si no era la que hiciste)
  if (aj_mejor_uci.length() >= 4 && aj_mejor_uci.substring(0, 4) != aj_uci.substring(0, 4) && aj_i > 0) {
    int x0, y0, x1, y1;
    aj_casilla(aj_mejor_uci[0] - 'a', '8' - aj_mejor_uci[1], x0, y0);
    aj_casilla(aj_mejor_uci[2] - 'a', '8' - aj_mejor_uci[3], x1, y1);
    x0 += 15; y0 += 15; x1 += 15; y1 += 15;
    float ang = atan2f(y1 - y0, x1 - x0);
    int xp = x1 - cosf(ang) * 10, yp = y1 - sinf(ang) * 10;
    uint16_t verde = C565(120, 180, 70);
    g.drawWideLine(x0, y0, xp, yp, 3.5f, verde);
    g.fillTriangle(x1, y1, xp + cosf(ang + 1.6f) * 9, yp + sinf(ang + 1.6f) * 9,
                   xp + cosf(ang - 1.6f) * 9, yp + sinf(ang - 1.6f) * 9, verde);
  }
  // el icono de la clase, en la esquina de la casilla de destino (como chess.com)
  if (a_c >= 0 && aj_clase) {
    int x, y;
    aj_casilla(a_c, a_f, x, y);
    g.fillCircle(x + 25, y + 5, 7, color_clase(aj_clase));
    g.drawCircle(x + 25, y + 5, 7, C565(255, 255, 255));
    g.setFont(&fonts::Font0);
    g.setTextDatum(middle_center);
    g.setTextColor(C565(255, 255, 255));
    g.drawString(simbolo_clase(aj_clase), x + 25, y + 5);
  }
  // abajo: la jugada, su clase, la mejor y la evaluación
  int yi = BARRA_H + 240;
  g.fillRect(0, yi, 240, 320 - yi, FONDO);
  patron(g, PAT_FLECHA_IZQ, 5, 8, yi + 18, 2, aj_i > 0 ? TXT2 : TXT3);
  patron(g, PAT_FLECHA_DER, 5, 224, yi + 18, 2, aj_i < aj_n ? TXT2 : TXT3);
  g.setTextDatum(middle_left);
  g.setFont(F_TXT);
  if (aj_i == 0) {
    g.setTextColor(TXT2);
    g.drawString(T("Posición inicial"), 24, yi + 14);
  } else {
    String jug = String(aj_num) + (aj_blancas ? ". " : "... ") + aj_san;
    g.setTextColor(TXT);
    g.drawString(jug, 24, yi + 14);
    g.setTextColor(color_clase(aj_clase));
    g.drawString(nombre_clase(aj_clase), 24 + g.textWidth(jug) + 8, yi + 14);
    g.setFont(F_PEQ);
    if (aj_mejor.length() && aj_mejor != aj_san && aj_clase != 'B' && aj_clase != 'K' && aj_clase != 'F') {
      g.setTextColor(TXT2);
      g.drawString(String(T("Mejor: ")) + aj_mejor, 24, yi + 34);
    }
    g.setTextDatum(middle_right);
    g.setTextColor(TXT3);
    g.drawString(aj_eval, 216, yi + 34);
  }
  if (aj_errores) {
    g.setFont(F_PEQ);
    g.setTextDatum(middle_right);
    g.setTextColor(color_clase('X'));
    g.drawString(T("solo errores"), 216, yi + 14);
  }
}

void fotograma_ajedrez() {
  g.fillScreen(FONDO);
  if (aj_vista == 1 && aj_estado == 2) aj_tablero();
  else aj_resumen();
  led(3, 1, 0);
  brillo_obj = 140;
  // si la jugada pedida no llega, volver a pedirla
  if (aj_vista == 1 && aj_ply_cargado != aj_i && ahora - aj_pedido > 1500) aj_ir(aj_i);
}

void ajedrez_toque(int x, int y) {
  SONAR(S_CLIC);
  interaccion();
  if (aj_vista == 0) {
    if (aj_estado == 0 && y >= 266) { pc_enviar("aj:ultima"); aj_estado = 1; aj_progreso = 0; return; }
    if (aj_estado != 2 || y < 274) return;
    aj_errores = x < 120;                        // "Mis errores" o "Revisar"
    aj_vista = 1;
    aj_i = 0;
    if (aj_errores) aj_paso(1); else aj_ir(0);
    return;
  }
  // tablero: abajo, izquierda = atrás, derecha = adelante, centro = volver al resumen
  int yi = BARRA_H + 240;
  if (y >= yi) {
    if (x < 70) aj_paso(-1);
    else if (x > 170) aj_paso(1);
    else aj_vista = 0;
  } else {
    aj_paso(x < 120 ? -1 : 1);                   // tocar el tablero: mitad izquierda atrás, derecha adelante
  }
}
