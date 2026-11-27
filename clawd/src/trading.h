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
// Se abre tocando la página "Bot" del panel. Arriba: los pares (BTC, ETH, SOL).
// Tocar la gráfica: velas de 1 h (2 días) <-> 4 h (8 días, donde se ven sus operaciones).
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

void tr_recibir(JsonObject o) {
  String par = o["par"] | "";
  bool es_4h = String(o["bar"] | "1H") == "4H";
  if (par != TR_PARES[tr_sel] || es_4h != tr_4h) return;   // llegó tarde: ya se cambió de par
  tr_llegada = ahora;
  tr_err = o["err"] | "";
  if (tr_err.length()) return;                              // sin red: se queda lo último
  tr_par_cargado = par;
  tr_precio = o["p"] | 0.0f;
  tr_var = o["var"] | 0.0f;
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
const int TR_GX = 4, TR_GY = 84, TR_GW = 192, TR_GH = 124;   // la gráfica
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
    int xf = TR_GX + TR_GW;
    if (tr_tp > 0) g.fillRect(xi, min(Y(tr_tp), Y(tr_e)), xf - xi, abs(Y(tr_e) - Y(tr_tp)) + 1, TR_VERDE_OSC);
    if (tr_st == "ab" && tr_sl0 > 0 && tr_sl0 < tr_e)   // el riesgo del principio
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
    int xi = tr_pos_i >= 0 ? x0 + tr_pos_i * paso : TR_GX, xf = TR_GX + TR_GW;
    if (tr_tp > 0) { g.drawFastHLine(xi, Y(tr_tp), xf - xi, VERDE); tr_etiqueta(Y(tr_tp), tr_precio_txt(tr_tp), VERDE, FONDO); }
    if (tr_sl > 0) {
      bool protegida = tr_st == "ab" && tr_sl >= tr_e;    // el stop ya está a la entrada o por encima
      uint16_t c = protegida ? AMARILLO : ROJO;
      g.drawFastHLine(xi, Y(tr_sl), xf - xi, c);
      tr_etiqueta(Y(tr_sl), tr_precio_txt(tr_sl), c, FONDO);
    }
    if (tr_st == "ab") { g.drawFastHLine(xi, Y(tr_e), xf - xi, TXT); }
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
  // precio actual: línea punteada y un punto que late en la última vela
  if (tr_nv) {
    int y = Y(tr_precio), x = x0 + (tr_nv - 1) * paso + 1;
    tr_linea_disc(x + 3, TR_GX + TR_GW, y, TXT2);
    float k = 0.5f + 0.5f * sinf(ahora / 260.0f);
    g.fillCircle(x, y, 2 + (int)(k * 2), TXT);
    tr_etiqueta(y, tr_precio_txt(tr_precio), TXT, FONDO);
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
  g.drawString(tr_4h ? T("velas 4 h · 8 días · toca: 1 h") : T("velas 1 h · 2 días · toca: 4 h"), TR_GX, TR_GY + TR_GH + 3);
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

void fotograma_trading() {
  g.fillScreen(FONDO);
  brillo_obj = 140;
  led(0, 2, 1);
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
  g.setTextColor(TXT3);
  g.drawString(String(tr_acierto) + T("% aciertos"), 234, yb);

  // volver sola a la mascota tras 3 min sin tocar
  if (ahora - ultima_interaccion > 180000) pantalla = PANT_MASCOTA;
}

void abrir_trading() {
  pantalla = PANT_TRADING;
  tr_pedido = 0;
  interaccion();
  SONAR(S_CLIC);
}

void trading_toque(int x, int y) {
  interaccion();
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
