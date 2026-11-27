#pragma once
// ------------------------------------------------------------
// trading.h — el bot de trading (~/Projects/trading-bot) en la placa
//
// SOLO MIRA: el PC (trading.py) lee los archivos del bot y las velas de OKX y manda:
//   {"tr_res": {...}}  el resumen para el panel de la mascota (cada minuto)
//   {"tr": {...}}      un par: velas, la posición o la orden, compras/ventas y la última decisión
// La placa pide el par que quiere ver ({"ev":"tr:ETH/USDC:4H"}) y lo vuelve a pedir
// cada 30 s mientras la pantalla está abierta: así la última vela "se mueve".
//
// Se abre tocando la página "Bot" del panel. Tres vistas:
//   0 gráfica: arriba los pares (BTC, ETH, SOL); tocar la gráfica = velas 1 h <-> 4 h;
//              tocar la fila de abajo ("Total…") abre la tabla de operaciones
//   1 tabla:   todas las operaciones ({"ev":"tr:ops"} -> {"tr_ops": [...]}); tocar una = su ficha
//   2 ficha:   una operación ({"ev":"tr:op:N"} -> {"tr_op"} + {"tr_opt"}): su gráfica, todos los
//              cálculos y el razonamiento de Claude, el cierre y el escéptico, por páginas
// ------------------------------------------------------------

// ---- resumen (panel) ----
bool tr_conocido = false;       // el companion ha encontrado el bot
float tr_pnl = 0, tr_pct = 0, tr_flot = 0, tr_prog = 0;   // prog: dónde va el precio entre stop (0) y objetivo (1)
int tr_cap = 0, tr_n = 0, tr_acierto = 0, tr_ab = 0, tr_pend = 0, tr_hace = -1;
bool tr_ok = false;

// ---- el par que se ve ----
const char *const TR_PARES[] = {"BTC/USDC", "ETH/USDC", "SOL/USDC"};
int tr_sel = 1;                 // ETH
bool tr_4h = true;              // 4 h por defecto: ahí se ven sus compras y ventas
const int TR_MAXV = 48;
float tr_v[TR_MAXV][4];         // o, h, l, c
int tr_nv = 0;
String tr_par_cargado, tr_err;
float tr_precio = 0, tr_var = 0;
float tr_mom = 0;               // % en 30 días: el régimen del bot (> 0 = puede comprar)
bool tr_mom_ok = false;
// posición viva: "ab" abierta, "pe" orden limit esperando, "" nada
String tr_st;
float tr_e = 0, tr_sl = 0, tr_tp = 0, tr_sl0 = 0, tr_pos_pnl = 0;
int tr_pos_i = -1;
struct TrMarca { int i; char tipo; float precio, pnl; };
TrMarca tr_m[8];
int tr_nm = 0;
String tr_acc, tr_plan;
int tr_dec_hace = -1;
uint32_t tr_pedido = 0, tr_llegada = 0, tr_desde = 0;
// en directo (WebSocket en el PC): último precio de cada par y cuándo cambió
float tr_vivo[3] = {0, 0, 0}, tr_vivo_pnl[3] = {0, 0, 0};
uint32_t tr_vivo_t = 0, tr_tick_t = 0;
int tr_tick_dir = 0;                  // +1 subió, -1 bajó (la etiqueta se ilumina un momento)

// ---- tabla y ficha de operaciones ----
int tr_vista = 0;               // 0 gráfica, 1 tabla, 2 ficha
struct TrFila { int n; uint8_t par; String fecha; char est; float pnl, r; bool hay_r; };
const int TR_MAXF = 40;
TrFila tr_filas[TR_MAXF];
int tr_nfilas = -1;             // -1 = aún no ha llegado la tabla
int tr_pag = 0;                 // página de la tabla o de la ficha
int tr_op_n = -1;               // operación que se ve en la ficha
bool tr_op_lista = false;       // ha llegado su gráfica y sus cálculos
int tr_op_par = 0;
float tr_op_pnl = 0;
char tr_op_est = 'c';
String tr_op_bar;
int tr_pos_f = -1;              // vela de la venta (la zona de la posición acaba ahí)
struct TrLinea { String a, b; uint8_t tipo, col; };   // tipo 0 cálculo, 1 texto, 2 título
const int TR_MAXL = 90;
TrLinea tr_lin[TR_MAXL];
int tr_nlin = 0;
TrLinea tr_calc[22];            // los cálculos tal cual llegan (para rehacer las líneas)
int tr_ncalc = 0;
String tr_op_texto;
uint32_t tr_op_pedido = 0;
int tr_gy = 84, tr_gh = 124;    // dónde va la gráfica (en la ficha, más pequeña)

void tr_pedir() {
  char b[24];
  snprintf(b, sizeof(b), "tr:%s:%s", TR_PARES[tr_sel], tr_4h ? "4H" : "1H");
  pc_enviar(b);
  tr_pedido = ahora;
}

void tr_recibir_resumen(JsonObject o) {
  tr_conocido = true;
  tr_pnl = o["pnl"] | 0.0f;  tr_pct = o["pct"] | 0.0f;  tr_flot = o["flot"] | 0.0f;
  tr_cap = o["cap"] | 0;     tr_n = o["n"] | 0;         tr_acierto = o["acierto"] | 0;
  tr_ab = o["ab"] | 0;       tr_pend = o["pend"] | 0;   tr_hace = o["hace"] | -1;
  tr_ok = (o["ok"] | 0) != 0;
  tr_prog = o["prog"] | 0.0f;
}

// {"trv": {"p": [BTC, ETH, SOL], "pnl": [...]}} cada segundo
void tr_recibir_vivo(JsonObject o) {
  JsonArray p = o["p"], pnl = o["pnl"];
  for (int k = 0; k < 3; k++) {
    float nuevo = p[k] | 0.0f;
    if (nuevo <= 0) continue;
    if (k == tr_sel && tr_vivo[k] > 0 && nuevo != tr_vivo[k]) { tr_tick_dir = nuevo > tr_vivo[k] ? 1 : -1; tr_tick_t = ahora; }
    tr_vivo[k] = nuevo;
    tr_vivo_pnl[k] = pnl[k] | 0.0f;
  }
  tr_vivo_t = ahora;
  // la última vela del par que se ve se mueve con el precio
  if (tr_vista == 0 && tr_nv > 0 && tr_par_cargado == TR_PARES[tr_sel] && tr_vivo[tr_sel] > 0) {
    float v = tr_vivo[tr_sel];
    tr_precio = v;
    tr_v[tr_nv - 1][3] = v;
    tr_v[tr_nv - 1][1] = max(tr_v[tr_nv - 1][1], v);
    tr_v[tr_nv - 1][2] = min(tr_v[tr_nv - 1][2], v);
    if (tr_st == "ab") tr_pos_pnl = tr_vivo_pnl[tr_sel];
  }
}

// {"tr_ev": {"t": "ETH: ¡objetivo!", "s": "1772 · +19,5 USDC", "bueno": 1}}: pasa algo de verdad
void aviso(const String &txt, uint32_t ms);
void notificar(uint8_t tipo, const String &titulo, const String &sub);
void tr_recibir_evento(JsonObject o) {
  String t = o["t"] | "", sub = o["s"] | "";
  bool bueno = (o["bueno"] | 0) != 0;
  notificar(3, t, sub);
  aviso(t + " · " + sub, 5000);
  if (bueno) SONAR(S_LISTO); else SONAR(S_ERROR);
}

void tr_recibir(JsonObject o) {
  String par = o["par"] | "";
  bool es_4h = String(o["bar"] | "1H") == "4H";
  if (tr_vista != 0 || par != TR_PARES[tr_sel] || es_4h != tr_4h) return;   // llegó tarde o se ve otra cosa
  tr_llegada = ahora;
  tr_err = o["err"] | "";
  if (tr_err.length()) return;                              // sin red: se queda lo último
  tr_par_cargado = par;
  tr_precio = o["p"] | 0.0f;
  tr_var = o["var"] | 0.0f;
  tr_mom_ok = o["mom"].is<float>();
  tr_mom = o["mom"] | 0.0f;
  JsonArray v = o["v"];
  tr_nv = 0;
  for (JsonVariant c : v) {
    if (tr_nv >= TR_MAXV) break;
    for (int k = 0; k < 4; k++) tr_v[tr_nv][k] = c[k] | 0.0f;
    tr_nv++;
  }
  JsonObject p = o["pos"];
  tr_st = p["st"] | "";
  tr_e = p["e"] | 0.0f;  tr_sl = p["sl"] | 0.0f;  tr_tp = p["tp"] | 0.0f;
  tr_sl0 = p["sl0"] | tr_sl;  tr_pos_pnl = p["pnl"] | 0.0f;  tr_pos_i = p["i"] | -1;
  tr_nm = 0;
  for (JsonVariant m : o["m"].as<JsonArray>()) {
    if (tr_nm >= 8) break;
    tr_m[tr_nm++] = {m[0] | -1, (m[1] | "c")[0], m[2] | 0.0f, m[3] | 0.0f};
  }
  JsonObject d = o["dec"];
  tr_acc = d["acc"] | "";
  tr_plan = d["plan"] | "";
  tr_dec_hace = d["hace"] | -1;
}

// precio con los decimales que tocan: 61.548 · 1.725,0 · 73,20
String tr_precio_txt(float p) {
  char b[16];
  if (p >= 10000) snprintf(b, sizeof(b), "%.0f", p);
  else if (p >= 1000) snprintf(b, sizeof(b), "%.1f", p);
  else snprintf(b, sizeof(b), "%.2f", p);
  return String(b);
}

String tr_usdc(float x) {                   // +14,7 / -118
  char b[16];
  snprintf(b, sizeof(b), fabsf(x) >= 100 ? "%+.0f" : "%+.1f", x);
  return String(b);
}

String tr_hace_txt(int min) {
  if (min < 0) return "";
  if (min < 60) return String(min) + " min";
  return String(min / 60) + " h";
}

// ---- panel de la mascota: una línea con lo esencial ----
// devuelve el texto y el color para fila_dato()
void tr_panel(String &val, float &frac, uint16_t &col) {
  if (!tr_ok) { val = T("parado"); frac = 0; col = ROJO; return; }
  val = tr_usdc(tr_pnl + tr_flot) + " USDC";
  if (tr_ab) val += " · " + String(tr_ab) + T(" abierta");
  frac = tr_ab ? constrain(tr_prog, 0.0f, 1.0f) : 0;   // la barra: del stop (vacía) al objetivo (llena)
  col = tr_flot > 0 ? VERDE : (tr_pnl + tr_flot < 0 ? TXT2 : VERDE);
}

// ---- la pantalla ----
const int TR_GX = 4, TR_GW = 192;           // la gráfica (alto y posición: tr_gy, tr_gh)
#define TR_GY tr_gy
#define TR_GH tr_gh
const uint16_t TR_VERDE_OSC = C565(30, 62, 40), TR_ROJO_OSC = C565(70, 30, 28);

void tr_linea_disc(int x0, int x1, int y, uint16_t col) {   // línea discontinua
  for (int x = x0; x < x1; x += 6) g.drawFastHLine(x, y, min(3, x1 - x), col);
}

// Las etiquetas de precio de la derecha se apuntan y se dibujan al final, separadas
// para que no se tapen (p. ej. el precio actual justo al lado del objetivo).
struct TrEtiq { int y; String txt; uint16_t fondo, letra; };
TrEtiq tr_etq[5];
int tr_netq = 0;
void tr_etiqueta(int y, const String &txt, uint16_t fondo, uint16_t letra) {
  if (tr_netq < 5) tr_etq[tr_netq++] = {y, txt, fondo, letra};
}
void tr_etiqueta_dibujar(int y, const String &txt, uint16_t fondo, uint16_t letra) {
  g.fillRoundRect(TR_GX + TR_GW + 2, y - 6, 238 - (TR_GX + TR_GW + 2), 12, 3, fondo);
  g.setTextColor(letra);
  g.setTextDatum(middle_center);
  g.drawString(txt, (TR_GX + TR_GW + 2 + 238) / 2, y);
}

void tr_grafica() {
  // escala: velas + niveles de la posición (para que el stop y el objetivo siempre se vean)
  tr_netq = 0;
  float lo = 1e12, hi = -1e12;
  for (int i = 0; i < tr_nv; i++) { lo = min(lo, tr_v[i][2]); hi = max(hi, tr_v[i][1]); }
  if (tr_st.length()) {
    float niveles[] = {tr_e, tr_sl, tr_tp};
    for (float n : niveles) if (n > 0) { lo = min(lo, n); hi = max(hi, n); }
  }
  float margen = (hi - lo) * 0.06f + 1e-6f;
  lo -= margen; hi += margen;
  auto Y = [&](float p) { return TR_GY + (int)lroundf((hi - p) / (hi - lo) * TR_GH); };

  // rejilla suave: 3 líneas con su precio
  g.setFont(&fonts::Font0);
  float etiquetas[] = {tr_precio, tr_st.length() ? tr_e : -1, tr_st.length() ? tr_sl : -1, tr_st.length() ? tr_tp : -1};
  for (int k = 1; k <= 3; k++) {
    float p = hi - (hi - lo) * k / 4;
    int y = Y(p);
    tr_linea_disc(TR_GX, TR_GX + TR_GW, y, SUP2);
    bool tapada = false;
    for (float e : etiquetas) if (e > 0 && abs(Y(e) - y) < 13) tapada = true;
    if (tapada) continue;
    g.setTextColor(TXT3);
    g.setTextDatum(middle_left);
    g.drawString(tr_precio_txt(p), TR_GX + TR_GW + 4, y);
  }

  int paso = TR_GW / TR_MAXV;                 // 4 px por vela
  int x0 = TR_GX + TR_GW - tr_nv * paso;      // pegadas a la derecha
  // la posición, como la herramienta "posición larga": zona verde hasta el objetivo, roja hasta el stop
  if (tr_st.length() && tr_e > 0) {
    int xi = tr_pos_i >= 0 ? x0 + tr_pos_i * paso : TR_GX;
    int xf = tr_pos_f >= 0 ? x0 + tr_pos_f * paso + 3 : TR_GX + TR_GW;
    if (tr_tp > 0) g.fillRect(xi, min(Y(tr_tp), Y(tr_e)), xf - xi, abs(Y(tr_e) - Y(tr_tp)) + 1, TR_VERDE_OSC);
    if ((tr_st == "ab" || tr_st == "ce") && tr_sl0 > 0 && tr_sl0 < tr_e)   // el riesgo del principio
      g.fillRect(xi, Y(tr_e), xf - xi, Y(tr_sl0) - Y(tr_e) + 1, TR_ROJO_OSC);
    else if (tr_sl > 0 && tr_sl < tr_e)
      g.fillRect(xi, Y(tr_e), xf - xi, Y(tr_sl) - Y(tr_e) + 1, TR_ROJO_OSC);
  }
  // velas
  for (int i = 0; i < tr_nv; i++) {
    float o = tr_v[i][0], h = tr_v[i][1], l = tr_v[i][2], c = tr_v[i][3];
    uint16_t col = c >= o ? VERDE : ROJO;
    int x = x0 + i * paso;
    g.drawFastVLine(x + 1, Y(h), max(1, Y(l) - Y(h)), col);
    int ya = Y(max(o, c)), yb = Y(min(o, c));
    g.fillRect(x, ya, 3, max(1, yb - ya), col);
  }
  // niveles de la posición
  if (tr_st.length() && tr_e > 0) {
    int xi = tr_pos_i >= 0 ? x0 + tr_pos_i * paso : TR_GX;
    int xf = tr_pos_f >= 0 ? x0 + tr_pos_f * paso + 3 : TR_GX + TR_GW;
    if (tr_tp > 0) { g.drawFastHLine(xi, Y(tr_tp), xf - xi, VERDE); tr_etiqueta(Y(tr_tp), tr_precio_txt(tr_tp), VERDE, FONDO); }
    if (tr_sl > 0) {
      bool protegida = tr_st != "pe" && tr_sl >= tr_e;    // el stop ya está a la entrada o por encima
      uint16_t c = protegida ? AMARILLO : ROJO;
      g.drawFastHLine(xi, Y(tr_sl), xf - xi, c);
      tr_etiqueta(Y(tr_sl), tr_precio_txt(tr_sl), c, FONDO);
    }
    if (tr_st == "ab" || tr_st == "ce") { g.drawFastHLine(xi, Y(tr_e), xf - xi, TXT); tr_etiqueta(Y(tr_e), tr_precio_txt(tr_e), TXT, FONDO); }
    else { tr_linea_disc(xi, xf, Y(tr_e), AMARILLO); tr_etiqueta(Y(tr_e), tr_precio_txt(tr_e), SUP2, AMARILLO); }
  }
  // compras (▲ verde debajo de la vela) y ventas (▼ arriba, verde si ganó, roja si perdió)
  for (int k = 0; k < tr_nm; k++) {
    const TrMarca &m = tr_m[k];
    if (m.i < 0 || m.i >= tr_nv) continue;
    int x = x0 + m.i * paso + 1, y = Y(m.precio);
    if (m.tipo == 'c') {
      g.fillTriangle(x, y + 3, x - 4, y + 9, x + 4, y + 9, CLAWD);
    } else {
      uint16_t c = m.pnl >= 0 ? VERDE : ROJO;
      g.fillTriangle(x, y - 3, x - 4, y - 9, x + 4, y - 9, c);
      g.setTextColor(c);
      g.setTextDatum(bottom_center);
      g.drawString(tr_usdc(m.pnl), constrain(x, 12, TR_GX + TR_GW - 12), max(TR_GY + 8, y - 10));
    }
  }
  // precio actual: línea punteada y un punto que late en la última vela (no en la ficha)
  if (tr_nv && tr_vista == 0) {
    int y = Y(tr_precio), x = x0 + (tr_nv - 1) * paso + 1;
    tr_linea_disc(x + 3, TR_GX + TR_GW, y, TXT2);
    float k = 0.5f + 0.5f * sinf(ahora / 260.0f);
    g.fillCircle(x, y, 2 + (int)(k * 2), TXT);
    bool tick = ahora - tr_tick_t < 400;
    tr_etiqueta(y, tr_precio_txt(tr_precio), tick ? (tr_tick_dir > 0 ? VERDE : ROJO) : TXT, FONDO);
  }
  g.drawRect(TR_GX - 1, TR_GY - 1, TR_GW + 2, TR_GH + 2, SUP);
  // separar las etiquetas: ordenarlas de arriba abajo y dejar 13 px entre ellas
  for (int a = 0; a < tr_netq; a++)
    for (int b = a + 1; b < tr_netq; b++)
      if (tr_etq[b].y < tr_etq[a].y) { TrEtiq t = tr_etq[a]; tr_etq[a] = tr_etq[b]; tr_etq[b] = t; }
  for (int a = 0; a < tr_netq; a++) tr_etq[a].y = constrain(tr_etq[a].y, TR_GY + 6, TR_GY + TR_GH - 6);
  for (int a = 1; a < tr_netq; a++) tr_etq[a].y = max(tr_etq[a].y, tr_etq[a - 1].y + 13);
  for (int a = tr_netq - 1; a >= 0; a--) {     // si se salen por abajo, empujar hacia arriba
    int tope = a == tr_netq - 1 ? TR_GY + TR_GH - 6 : tr_etq[a + 1].y - 13;
    tr_etq[a].y = min(tr_etq[a].y, tope);
  }
  for (int a = 0; a < tr_netq; a++) tr_etiqueta_dibujar(tr_etq[a].y, tr_etq[a].txt, tr_etq[a].fondo, tr_etq[a].letra);
  // pie: 1h / 4h y cuánto abarca
  g.setFont(F_PEQ);
  g.setTextDatum(top_left);
  g.setTextColor(TXT3);
  if (tr_vista == 0)
    g.drawString(tr_4h ? T("velas 4 h · 8 días · toca: 1 h") : T("velas 1 h · 2 días · toca: 4 h"), TR_GX, TR_GY + TR_GH + 3);
  else
    g.drawString(String(T("velas de ")) + (tr_op_bar == "1H" ? "1 h" : tr_op_bar == "4H" ? "4 h" : "1 d"), TR_GX, TR_GY + TR_GH + 3);
}

void tr_decision(int y) {
  g.setFont(F_PEQ);
  g.setTextDatum(top_left);
  // la etiqueta de la acción, como un botón pequeño
  String acc = tr_acc == "comprar" ? T("COMPRAR") : tr_acc == "vender" ? T("VENDER") : T("ESPERAR");
  uint16_t c = tr_acc == "comprar" ? VERDE : tr_acc == "vender" ? ROJO : SUP2;
  int w = g.textWidth(acc) + 12;
  g.fillRoundRect(8, y, w, 16, 4, c);
  g.setTextColor(tr_acc == "comprar" || tr_acc == "vender" ? FONDO : TXT);
  g.setTextDatum(middle_left);
  g.drawString(acc, 14, y + 8);
  g.setTextColor(TXT3);
  String info = String(T("Claude decidió")) + (tr_dec_hace >= 0 ? " · " + String(T("hace ")) + tr_hace_txt(tr_dec_hace) : "");
  g.drawString(recortar(info, 230 - w - 14), 8 + w + 6, y + 8);
  String lineas[3];
  int n = envolver(tr_plan, 37, lineas, 3);
  g.setTextColor(TXT2);
  for (int i = 0; i < n; i++) texto_mixto(g, lineas[i], 8, y + 19 + i * 14, F_PEQ, top_left);
}


// ================== tabla y ficha de operaciones ==================
void tr_recibir_ops(JsonArray a) {
  tr_nfilas = 0;
  for (JsonVariant v : a) {
    if (tr_nfilas >= TR_MAXF) break;
    TrFila &f = tr_filas[tr_nfilas++];
    f.n = v[0] | -1;  f.par = v[1] | 0;  f.fecha = v[2] | "";  f.est = (v[3] | "c")[0];
    f.pnl = v[4] | 0.0f;  f.hay_r = !v[5].isNull();  f.r = v[5] | 0.0f;
  }
}

// las líneas de la ficha: primero los cálculos, luego el texto partido en renglones
void tr_lineas_rehacer() {
  tr_nlin = 0;
  for (int i = 0; i < tr_ncalc && tr_nlin < TR_MAXL; i++) tr_lin[tr_nlin++] = tr_calc[i];
  int ini = 0;
  while (ini < (int)tr_op_texto.length() && tr_nlin < TR_MAXL - 1) {
    int fin = tr_op_texto.indexOf('\n', ini);
    if (fin < 0) fin = tr_op_texto.length();
    String parrafo = tr_op_texto.substring(ini, fin);
    ini = fin + 1;
    if (parrafo.startsWith("§")) {                        // un título de sección
      tr_lin[tr_nlin++] = {"", "", 1, 0};                  // renglón en blanco antes
      if (tr_nlin < TR_MAXL) tr_lin[tr_nlin++] = {parrafo.substring(2), "", 2, 0};   // "§" ocupa 2 bytes
      continue;
    }
    String trozos[24];
    int n = envolver(parrafo, 37, trozos, 24);
    for (int k = 0; k < n && tr_nlin < TR_MAXL; k++) tr_lin[tr_nlin++] = {trozos[k], "", 1, 0};
  }
}

void tr_recibir_op(JsonObject o) {
  if ((o["id"] | -2) != tr_op_n) return;                  // llegó tarde: ya se pidió otra
  if (!o["err"].isNull()) { tr_op_lista = true; tr_nv = 0; tr_ncalc = 0; tr_lineas_rehacer(); return; }
  tr_op_par = o["par"] | 0;
  tr_op_bar = o["bar"] | "1H";
  tr_op_est = (o["est"] | "c")[0];
  tr_op_pnl = o["pnl"] | 0.0f;
  // la gráfica usa las mismas variables que la vista normal (al volver se vuelve a pedir el par)
  tr_par_cargado = "";
  tr_nv = 0;
  for (JsonVariant c : o["v"].as<JsonArray>()) {
    if (tr_nv >= TR_MAXV) break;
    for (int k = 0; k < 4; k++) tr_v[tr_nv][k] = c[k] | 0.0f;
    tr_nv++;
  }
  tr_st = tr_op_est == 'a' ? "ab" : tr_op_est == 'p' ? "pe" : "ce";
  tr_e = o["e"] | 0.0f;  tr_sl0 = o["sl"] | 0.0f;  tr_tp = o["tp"] | 0.0f;
  tr_sl = o["slf"] | tr_sl0;
  if (tr_sl <= 0) tr_sl = tr_sl0;
  tr_pos_i = o["i"] | -1;
  tr_nm = 0;
  tr_pos_f = -1;
  for (JsonVariant m : o["m"].as<JsonArray>()) {
    if (tr_nm >= 8) break;
    tr_m[tr_nm++] = {m[0] | -1, (m[1] | "c")[0], m[2] | 0.0f, m[3] | 0.0f};
    if (tr_m[tr_nm - 1].tipo == 'v') tr_pos_f = tr_m[tr_nm - 1].i;
  }
  if (tr_nv) tr_precio = tr_v[tr_nv - 1][3];
  tr_ncalc = 0;
  for (JsonVariant c : o["calc"].as<JsonArray>()) {
    if (tr_ncalc >= 22) break;
    tr_calc[tr_ncalc++] = {c[0] | "", c[1] | "", 0, (uint8_t)(c[2] | 0)};
  }
  tr_op_lista = true;
  tr_lineas_rehacer();
}

void tr_recibir_opt(JsonObject o) {
  if ((o["id"] | -2) != tr_op_n) return;
  tr_op_texto = o["t"] | "";
  tr_lineas_rehacer();
}

void tr_abrir_tabla() {
  tr_vista = 1;
  tr_pag = 0;
  tr_nfilas = -1;
  pc_enviar("tr:ops");
}

void tr_abrir_ficha(int n) {
  tr_vista = 2;
  tr_op_n = n;
  tr_op_lista = false;
  tr_op_texto = "";
  tr_ncalc = 0;
  tr_nlin = 0;
  tr_pag = 0;
  char b[20];
  snprintf(b, sizeof(b), "tr:op:%d", n);
  pc_enviar(b);
  tr_op_pedido = ahora;
}

void tr_volver_grafica() {
  tr_vista = 0;
  tr_gy = 84; tr_gh = 124;
  tr_nv = 0; tr_par_cargado = ""; tr_pedido = 0; tr_pos_f = -1;
}

void tr_cabecera(const String &titulo) {
  patron(g, PAT_FLECHA_IZQ, 5, 8, BARRA_H + 9, 2, TXT2);
  g.setFont(F_TXT);
  g.setTextDatum(middle_left);
  g.setTextColor(TXT);
  g.drawString(titulo, 22, BARRA_H + 15);
}

void tr_paginas(int pag, int total) {               // ‹ 2/5 › abajo
  if (total <= 1) return;
  g.setFont(F_PEQ);
  g.setTextDatum(middle_center);
  g.setTextColor(TXT2);
  g.drawString(String(pag + 1) + "/" + String(total), 120, 310);
  patron(g, PAT_FLECHA_IZQ, 5, 20, 305, 2, pag > 0 ? TXT2 : SUP2);
  // flecha derecha: la izquierda dada la vuelta
  const char *const der[] = {"X..", "XX.", "XXX", "XX.", "X.."};
  patron(g, der, 5, 214, 305, 2, pag < total - 1 ? TXT2 : SUP2);
}

const int TR_FILAS_PAG = 12, TR_FILA_H = 19, TR_FILA_Y = 70;

void fotograma_tabla() {
  char b[24];
  tr_cabecera(T("Operaciones"));
  g.setFont(F_PEQ);
  g.setTextDatum(middle_right);
  g.setTextColor(TXT3);
  g.drawString(String(tr_n) + T(" cerradas · ") + String(tr_acierto) + " %", 234, BARRA_H + 15);
  if (tr_nfilas < 0) {
    g.setTextDatum(middle_center);
    g.drawString(T("Cargando…"), 120, 150);
    return;
  }
  // títulos de columna
  int y = 56;
  g.setTextColor(TXT3);
  g.setTextDatum(middle_left);
  g.drawString(T("Par"), 8, y);
  g.drawString(T("Fecha"), 44, y);
  g.setTextDatum(middle_right);
  g.drawString("USDC", 128, y);
  g.drawString("R", 170, y);
  g.drawFastHLine(8, y + 7, 224, SUP);
  int paginas = (tr_nfilas + TR_FILAS_PAG - 1) / TR_FILAS_PAG;
  tr_pag = constrain(tr_pag, 0, max(0, paginas - 1));
  for (int k = 0; k < TR_FILAS_PAG; k++) {
    int i = tr_pag * TR_FILAS_PAG + k;
    if (i >= tr_nfilas) break;
    const TrFila &f = tr_filas[i];
    int yf = TR_FILA_Y + k * TR_FILA_H + TR_FILA_H / 2;
    if (k % 2) g.fillRect(4, yf - TR_FILA_H / 2, 232, TR_FILA_H, C565(20, 20, 19));
    String par = TR_PARES[min((int)f.par, 2)];
    g.setTextDatum(middle_left);
    g.setTextColor(TXT);
    g.drawString(par.substring(0, par.indexOf('/')), 8, yf);
    g.setTextColor(TXT3);
    g.drawString(f.fecha, 44, yf);
    g.setTextDatum(middle_right);
    if (f.est == 'c') {
      g.setTextColor(f.pnl > 0 ? VERDE : f.pnl < 0 ? ROJO : TXT2);
      snprintf(b, sizeof(b), "%+.2f", f.pnl);
      g.drawString(b, 128, yf);
    } else {
      g.setTextColor(f.est == 'a' ? AMARILLO : TXT3);
      g.drawString(f.est == 'a' ? T("abierta") : f.est == 'p' ? T("esperando") : T("revisar"), 128, yf);
    }
    if (f.hay_r) {
      g.setTextColor(f.r > 0 ? VERDE : ROJO);
      snprintf(b, sizeof(b), "%+.1f", f.r);
      g.drawString(b, 170, yf);
      // barrita de R: el cero en x = 190, 14 px por R (de -1 R a +3 R)
      int largo = (int)lroundf(constrain(f.r, -1.0f, 3.0f) * 14);
      g.drawFastVLine(190, yf - 6, 12, SUP2);
      if (largo > 0) g.fillRect(191, yf - 4, largo, 8, VERDE);
      else if (largo < 0) g.fillRect(190 + largo, yf - 4, -largo, 8, ROJO);
    }
  }
  tr_paginas(tr_pag, paginas);
}

const int TR_LIN_Y = 166, TR_LIN_H = 14, TR_LIN_PAG = 9;

void fotograma_ficha() {
  char b[24];
  String par = TR_PARES[min(tr_op_par, 2)];
  tr_cabecera(par.substring(0, par.indexOf('/')) + (tr_nfilas > 0 ? "" : ""));
  if (!tr_op_lista) {
    g.setFont(F_PEQ);
    g.setTextDatum(middle_center);
    g.setTextColor(TXT3);
    g.drawString(T("Cargando…"), 120, 150);
    if (ahora - tr_op_pedido > 5000) tr_abrir_ficha(tr_op_n);   // no llegó: pedirla otra vez
    return;
  }
  // el resultado, como una etiqueta arriba a la derecha
  g.setFont(F_PEQ);
  String res = tr_op_est == 'c' ? String(tr_op_pnl >= 0 ? "+" : "") + String(tr_op_pnl, 2) + " USDC"
             : tr_op_est == 'a' ? String(T("abierta")) : String(T("esperando"));
  uint16_t col = tr_op_est != 'c' ? AMARILLO : tr_op_pnl > 0 ? VERDE : ROJO;
  int w = g.textWidth(res) + 12;
  g.fillRoundRect(234 - w, BARRA_H + 6, w, 18, 5, col);
  g.setTextColor(FONDO);
  g.setTextDatum(middle_center);
  g.drawString(res, 234 - w / 2, BARRA_H + 15);
  // la gráfica de la operación
  tr_gy = 54; tr_gh = 92;
  if (tr_nv) tr_grafica();
  // los cálculos y el razonamiento, por páginas
  int paginas = max(1, (tr_nlin + TR_LIN_PAG - 1) / TR_LIN_PAG);
  tr_pag = constrain(tr_pag, 0, paginas - 1);
  for (int k = 0; k < TR_LIN_PAG; k++) {
    int i = tr_pag * TR_LIN_PAG + k;
    if (i >= tr_nlin) break;
    const TrLinea &l = tr_lin[i];
    int y = TR_LIN_Y + k * TR_LIN_H;
    if (l.tipo == 0) {                               // cálculo: etiqueta | valor
      g.setFont(F_PEQ);
      g.setTextDatum(top_left);
      g.setTextColor(TXT3);
      g.drawString(l.a, 8, y);
      int hueco = 226 - g.textWidth(l.a) - 8;
      g.setTextDatum(top_right);
      g.setTextColor(l.col == 1 ? VERDE : l.col == 2 ? ROJO : l.col == 3 ? AMARILLO : TXT);
      g.drawString(recortar(l.b, hueco), 232, y);
    } else if (l.tipo == 2) {                        // título de sección
      g.setTextColor(CLAWD);
      texto_mixto(g, l.a, 8, y, F_PEQ, top_left);
    } else {
      g.setTextColor(TXT2);
      texto_mixto(g, l.a, 8, y, F_PEQ, top_left);
    }
  }
  tr_paginas(tr_pag, paginas);
  (void)b;
}

void fotograma_trading() {
  g.fillScreen(FONDO);
  brillo_obj = 140;
  led(0, 2, 1);
  if (ahora - ultima_interaccion > 180000) { tr_volver_grafica(); pantalla = PANT_MASCOTA; return; }
  if (tr_vista == 1) { fotograma_tabla(); return; }
  if (tr_vista == 2) { fotograma_ficha(); return; }
  tr_gy = 84; tr_gh = 124;
  // pedir el par: al entrar, al cambiar y cada 30 s (si no llega en 4 s, otra vez)
  bool cargado = tr_par_cargado == TR_PARES[tr_sel] && tr_nv > 0;
  if (!tr_pedido || ahora - tr_pedido > (cargado && tr_llegada >= tr_pedido ? 30000u : 4000u)) tr_pedir();

  // fila 1: ‹ y los tres pares
  patron(g, PAT_FLECHA_IZQ, 5, 8, BARRA_H + 9, 2, TXT2);
  for (int k = 0; k < 3; k++) {
    int x = 22 + k * 56;
    bool sel = k == tr_sel;
    g.fillRoundRect(x, BARRA_H + 4, 52, 22, 7, sel ? TXT : SUP);
    g.setFont(F_TXT);
    g.setTextDatum(middle_center);
    g.setTextColor(sel ? FONDO : TXT2);
    String n = TR_PARES[k];
    g.drawString(n.substring(0, n.indexOf('/')), x + 26, BARRA_H + 15);
  }
  // demo, y si el bot está vivo
  g.setFont(F_PEQ);
  g.setTextDatum(middle_right);
  g.setTextColor(tr_ok ? TXT3 : ROJO);
  g.drawString(tr_ok ? "demo" : T("parado"), 234, BARRA_H + 15);
  if (ahora - tr_vivo_t < 3000)                  // punto que late: el precio llega en directo
    g.fillCircle(234 - g.textWidth(tr_ok ? "demo" : T("parado")) - 7, BARRA_H + 15, 2 + ((ahora / 500) % 2), ROJO);

  if (!cargado) {
    g.setTextDatum(middle_center);
    g.setTextColor(TXT3);
    g.drawString(tr_err.length() ? String(T("Sin datos: ")) + tr_err : String(T("Cargando…")), 120, 150);
    return;
  }
  // fila 2: precio grande, cambio en 24 h y el estado de la posición
  int y = BARRA_H + 34;
  g.setFont(F_GRANDE);
  g.setTextDatum(top_left);
  g.setTextColor(TXT);
  String pr = tr_precio_txt(tr_precio);
  g.drawString(pr, 8, y);
  int x = 8 + g.textWidth(pr) + 6;
  g.setFont(F_PEQ);
  g.setTextColor(tr_var >= 0 ? VERDE : ROJO);
  char b[12];
  snprintf(b, sizeof(b), "%+.1f%%", tr_var);
  g.drawString(b, x, y + 4);
  g.setTextDatum(top_right);
  if (tr_st == "ab") {
    g.setTextColor(tr_pos_pnl >= 0 ? VERDE : ROJO);
    g.drawString(String(T("dentro · ")) + tr_usdc(tr_pos_pnl), 234, y + 4);
  } else if (tr_st == "pe") {
    g.setTextColor(AMARILLO);
    g.drawString(String(T("espera a ")) + tr_precio_txt(tr_e), 234, y + 4);
  } else if (tr_mom_ok && tr_mom <= 0) {        // la regla de 30 días no deja comprar
    g.setTextColor(ROJO);
    snprintf(b, sizeof(b), "%+.0f%%", tr_mom);
    g.drawString(String(T("bloqueado · 30 d ")) + b, 234, y + 4);
  } else {
    g.setTextColor(TXT3);
    g.drawString(T("fuera"), 234, y + 4);
  }

  tr_grafica();
  tr_decision(TR_GY + TR_GH + 18);

  // abajo: el resultado de verdad (lo cerrado + lo que va ahora)
  int yb = 300;
  g.drawFastHLine(8, yb - 5, 224, SUP);
  g.setFont(F_PEQ);
  g.setTextDatum(top_left);
  g.setTextColor(TXT3);
  g.drawString(T("Total"), 8, yb);
  g.setTextColor(tr_pnl + tr_flot >= 0 ? VERDE : ROJO);
  snprintf(b, sizeof(b), " (%+.1f%%)", tr_pct);
  g.drawString(tr_usdc(tr_pnl + tr_flot) + " USDC" + b, 8 + g.textWidth(T("Total")) + 6, yb);
  g.setTextDatum(top_right);
  g.setTextColor(TXT2);
  g.drawString(String(tr_acierto) + T("% aciertos") + "  ›", 234, yb);

}

void abrir_trading() {
  pantalla = PANT_TRADING;
  tr_volver_grafica();
  tr_pedido = 0;
  interaccion();
  SONAR(S_CLIC);
}

void trading_toque(int x, int y) {
  interaccion();
  if (tr_vista == 1) {                                  // la tabla
    SONAR(S_CLIC);
    if (y < BARRA_H + 30) { if (x < 120) tr_volver_grafica(); return; }
    if (y >= 298) { tr_pag += x < 120 ? -1 : 1; return; }
    int k = (y - TR_FILA_Y) / TR_FILA_H, i = tr_pag * TR_FILAS_PAG + k;
    if (y >= TR_FILA_Y && k < TR_FILAS_PAG && i < tr_nfilas) tr_abrir_ficha(tr_filas[i].n);
    return;
  }
  if (tr_vista == 2) {                                  // la ficha
    SONAR(S_CLIC);
    if (y < BARRA_H + 30) { if (x < 120) { tr_vista = 1; tr_pag = 0; } return; }
    if (y >= TR_LIN_Y - 16) tr_pag += x < 120 ? -1 : 1;  // abajo: páginas (izquierda atrás, derecha adelante)
    return;
  }
  if (y >= 292) { SONAR(S_CLIC); tr_abrir_tabla(); return; }   // "Total … ›": la tabla
  if (y < BARRA_H + 30) {
    if (x < 20) { pantalla = PANT_MASCOTA; SONAR(S_CLIC); return; }   // ‹ volver
    int k = (x - 22) / 56;
    if (k >= 0 && k < 3 && k != tr_sel) { tr_sel = k; tr_nv = 0; tr_pedido = 0; SONAR(S_CLIC); }
    return;
  }
  if (y >= TR_GY && y < TR_GY + TR_GH + 18) {   // la gráfica: 1 h <-> 4 h
    tr_4h = !tr_4h;
    tr_nv = 0;
    tr_pedido = 0;
    SONAR(S_CLIC);
  }
}
