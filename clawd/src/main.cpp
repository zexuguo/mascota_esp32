// ============================================================
//  Clawd — el cangrejito de Claude Code como mascota de escritorio
//
//  Cada fotograma (25 por segundo) el loop hace 4 cosas:
//    1. LEER:     táctil, micrófono y datos del PC
//    2. DECIDIR:  qué está haciendo Clawd (el "modo", por prioridades)
//    3. DIBUJAR:  todo en un lienzo en memoria (sprite) ...
//    4. ENSEÑAR:  ... y el lienzo entero de golpe a la pantalla (sin parpadeos)
//
//  Qué sabe hacer:
//    - Pasear, mirar, parpadear y respirar él solo
//    - Toque: salta.  Doble toque: pomodoro de 25 min.  Dejar el dedo: mimos
//    - Tocar el suelo: va hacia allí.  Tocar arriba: silencio on/off
//    - Tocar el panel de abajo: cambia la información
//    - Palmada: se asusta.  Música: baila
//    - Claude Code trabajando: se sienta a teclear con el spinner
//    - Claude Code pide permiso: te avisa.  Claude termina: confeti
//    - Portátil muy caliente: suda.  Poca batería: cansado
//    - Se duerme si no le haces caso (antes por la noche)
//    - Cada 50 min te recuerda beber agua, estirarte...
// ============================================================
#include "placa.h"
#include "audio.h"
#include "tactil.h"
#include "pc.h"
#include "clawd.h"
#include "animaciones.h"
#include <mbedtls/base64.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

Preferences ajustes;       // memoria que no se borra al apagar (mute, micro, récords)
#include "juegos.h"


// "Migas de pan": en qué paso del loop estamos. Vive en una memoria que NO se
// borra al reiniciar, así que si la placa se cuelga y el watchdog la reinicia,
// al arrancar sabemos dónde se quedó.
RTC_NOINIT_ATTR uint32_t miga;
uint32_t miga_anterior = 0;
esp_reset_reason_t motivo_arranque;
bool arranque_contado = false;

const char *nombre_motivo(esp_reset_reason_t m) {
  switch (m) {
    case ESP_RST_POWERON:  return "encendido";
    case ESP_RST_SW:       return "reinicio normal";
    case ESP_RST_PANIC:    return "ERROR (panic)";
    case ESP_RST_TASK_WDT: return "COLGADA (watchdog)";
    case ESP_RST_INT_WDT:  return "COLGADA (watchdog de interrupciones)";
    case ESP_RST_WDT:      return "COLGADA (watchdog)";
    case ESP_RST_BROWNOUT: return "le falto corriente";
    case ESP_RST_USB:      return "reinicio por USB";
    default:               return "otro";
  }
}

LGFX tft;
LGFX_Sprite g(&tft);   // el lienzo: 240x320 en la PSRAM

const int SUELO   = 216;   // dónde pisa Clawd (múltiplo de L = 4)
const int ARRIBA  = 30;    // fin de la barra de arriba
const int PANEL_Y = 254;   // empieza el panel de abajo

uint32_t ahora = 0;

// ---------- Capturas de pantalla (para revisar el diseño desde el PC) ----------
// El PC manda {"cmd":"captura"}; al acabar el fotograma, la placa le manda el
// lienzo entero (240 x 320 colores de 16 bits) en trozos base64 de 4 filas.
bool pedir_captura = false;

void enviar_captura(LGFX_Sprite &lienzo, int pant) {
  const uint8_t *buf = (const uint8_t *)lienzo.getBuffer();
  if (!buf) return;
  static unsigned char b64[2700];
  Serial.printf("{\"cap\":\"ini\",\"w\":240,\"h\":320,\"p\":%d}\n", pant);
  for (int y = 0; y < 320; y += 4) {
    size_t len = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &len, buf + y * 480, 480 * 4);
    Serial.printf("{\"cy\":%d,\"cd\":\"", y);
    Serial.write(b64, len);
    Serial.print("\"}\n");
    esp_task_wdt_reset();
  }
  Serial.print("{\"cap\":\"fin\"}\n");
}

// ---------- Reacciones: cosas que duran un rato y luego se acaban ----------
enum Reaccion { R_NADA, R_HOLA, R_SALTO, R_CONTENTO, R_SUSTO, R_LISTO, R_DESPERTAR, R_POMO_FIN, R_DESCANSO_FIN };
Reaccion reac = R_NADA;
uint32_t reac_hasta = 0, reac_desde = 0;

void reaccionar(Reaccion r, uint32_t ms) {
  reac = r;
  reac_desde = ahora;
  reac_hasta = ahora + ms;
}

// ---------- Cuerpo ----------
float cx = 120, objetivo_x = 120;
int8_t dir = 1;
bool andando = false;
float alto = 0, vy = 0;   // salto
uint32_t aplastado_hasta = 0;   // al caer, unos fotogramas aplastado
uint32_t prox_accion = 0, prox_parpadeo = 0, fin_parpadeo = 0, prox_mirada = 0;
int8_t mirada = 0;

void saltar(float fuerza) {
  if (alto <= 0) vy = -fuerza;
}

// ---------- Estado general ----------
uint32_t ultima_interaccion = 0;
bool durmiendo = false;
float brillo = 0;

// táctil
bool tocaba = false, toque_en_clawd = false, mimando = false;
uint32_t toque_ini = 0, ultimo_tap = 0;
int16_t tx0 = 0, ty0 = 0;

// micrófono
float suelo_ruido = 60, ruido_suave = 0;
uint32_t musica_desde = 0, ultima_palmada = 0;
bool bailando = false;
float nivel_voz = 0;       // volumen mientras hablas (para la barrita)

// Claude Code
String claude_prev = "";
uint32_t claude_desde = 0;

// pomodoro
enum Pomo { POMO_NO, POMO_FOCO, POMO_DESCANSO };
Pomo pomo = POMO_NO;
uint32_t pomo_ini = 0, pomo_fin = 0;
const uint32_t FOCO_MS = 25UL * 60 * 1000, DESCANSO_MS = 5UL * 60 * 1000;

// temporizador (como el del móvil)
enum Temp { T_PARADO, T_CORRIENDO, T_PAUSA, T_SONANDO };
Temp temp = T_PARADO;
uint32_t temp_dur = 5 * 60;      // segundos elegidos
uint32_t temp_fin = 0;           // millis() en que acaba (corriendo)
uint32_t temp_queda = 0;         // ms que quedaban al pausar
uint32_t temp_suena_desde = 0, temp_prox_pitido = 0;
bool reloj_abierto = false;      // la pantalla del temporizador, encima de todo
// la rueda del temporizador
const int RUEDA_X = 120, RUEDA_Y = 156, RUEDA_R = 84;   // centro y radio
bool rueda_arrastre = false;
int rueda_modo = 1;              // qué eliges con la rueda: 0 = horas, 1 = minutos


// recordatorios
const char *const RECORDATORIOS[] = {"Bebe agua!", "Estirate!", "Mira lejos", "Respira hondo", "Espalda recta!"};
int recordatorio = -1;
uint32_t prox_recordatorio = 50UL * 60 * 1000, rec_hasta = 0;

// panel de abajo
int pagina = 0;
uint32_t prox_pagina = 0;
String aviso_txt = "";
uint32_t aviso_hasta = 0;
bool pc_estaba = false;


// Los verbos del spinner de Claude Code (los mismos que salen en la terminal)
const char *const VERBOS[] = {
  "Accomplishing", "Actioning", "Actualizing", "Architecting", "Baking", "Beaming", "Beboppin'",
  "Befuddling", "Billowing", "Blanching", "Bloviating", "Boogieing", "Boondoggling", "Booping",
  "Bootstrapping", "Brewing", "Bunning", "Burrowing", "Calculating", "Caramelizing", "Cascading",
  "Catapulting", "Cerebrating", "Channeling", "Choreographing", "Churning", "Clauding",
  "Coalescing", "Cogitating", "Combobulating", "Composing", "Computing", "Concocting",
  "Considering", "Contemplating", "Cooking", "Crafting", "Creating", "Crunching", "Crystallizing",
  "Cultivating", "Deciphering", "Deliberating", "Determining", "Dilly-dallying",
  "Discombobulating", "Doing", "Doodling", "Drizzling", "Ebbing", "Effecting", "Elucidating",
  "Embellishing", "Enchanting", "Envisioning", "Fermenting", "Fiddle-faddling", "Finagling",
  "Flambeing", "Flibbertigibbeting", "Flowing", "Flummoxing", "Fluttering", "Forging", "Forming",
  "Frolicking", "Frosting", "Gallivanting", "Galloping", "Garnishing", "Generating",
  "Gesticulating", "Germinating", "Gitifying", "Grooving", "Gusting", "Harmonizing", "Hashing",
  "Hatching", "Herding", "Honking", "Hullaballooing", "Hyperspacing", "Ideating", "Imagining",
  "Improvising", "Incubating", "Inferring", "Infusing", "Ionizing", "Jitterbugging", "Julienning",
  "Kerfuffling", "Kneading", "Leavening", "Levitating", "Lollygagging", "Manifesting",
  "Marinating", "Meandering", "Metamorphosing", "Misting", "Moonwalking", "Moseying", "Mulling",
  "Mustering", "Nebulizing", "Nesting", "Newspapering", "Noodling", "Nucleating", "Onioning",
  "Orbiting", "Orchestrating", "Osmosing", "Perambulating", "Percolating", "Perusing",
  "Philosophizing", "Photosynthesizing", "Polishing", "Pollinating", "Pondering", "Pontificating",
  "Pouncing", "Precipitating", "Prestidigitating", "Processing", "Proofing", "Propagating",
  "Puttering", "Puzzling", "Quantumizing", "Razzle-dazzling", "Razzmatazzing", "Recombobulating",
  "Reticulating", "Roosting", "Ruminating", "Sauteing", "Scampering", "Schlepping", "Scurrying",
  "Seasoning", "Shenaniganing", "Shimmying", "Simmering", "Skedaddling", "Sketching", "Slithering",
  "Smooshing", "Sock-hopping", "Spelunking", "Spinning", "Sprouting", "Stewing", "Sublimating",
  "Swirling", "Swooping", "Symbioting", "Synthesizing", "Tempering", "Thinking", "Thundering",
  "Tinkering", "Tomfoolering", "Topsy-turvying", "Transfiguring", "Transmogrifying", "Transmuting",
  "Twisting", "Undulating", "Unfurling", "Unraveling", "Vibing", "Waddling", "Wandering",
  "Warping", "Whatchamacalliting", "Whirlpooling", "Whirring", "Whisking", "Wibbling", "Working",
  "Wrangling", "Zesting", "Zigzagging"
};
const int N_VERBOS = sizeof(VERBOS) / sizeof(VERBOS[0]);

// ---------- Pantallas: la mascota o la "terminal" de Claude Code ----------
enum Pantalla { PANT_MASCOTA, PANT_CLAUDE, PANT_TERMINAL, PANT_GALAXIA, PANT_JUEGOS };
Pantalla pantalla = PANT_MASCOTA;

// ---------- Monitor de Claude Code (lo que hace en el PC) ----------
String mon_proyecto = "", mon_resumen = "";
struct Tarea { uint8_t estado; char txt[41]; };   // estado: 0 pendiente, 1 en curso, 2 hecha
Tarea mon_tareas[12];
int mon_n = 0;
const int MON_ACTS = 4;
String mon_acts[MON_ACTS];                 // los últimos pasos (el más nuevo, el último)
uint32_t claude_duro = 0, claude_fin = 0;  // cuánto tardó la última vez y cuándo acabó

// ---------- El aviso grande: "Claude ha terminado" / "Claude te necesita" ----------
enum Alerta { AL_NO, AL_LISTO, AL_ESPERA, AL_TIEMPO };
Alerta alerta = AL_NO;
bool toque_aviso = false;                  // este dedo ha caído sobre el aviso

// ---------- Permiso pedido por Claude Code (con la tapa cerrada) ----------
bool perm_activo = false;
String perm_id, perm_herr, perm_det, perm_proy;
uint32_t perm_desde = 0, perm_prox_pitido = 0;
bool toque_permiso = false;

// ---------- Voz ----------
//  ESCUCHA:    grabando mientras mantienes pulsado (o el botón BOOT)
//  TRANSCRIBE: el PC convierte la voz en texto (Whisper)
//  CUENTA:     3 s para cancelar antes de mandárselo a Claude
//  CLAUDE:     Claude Code trabajando en el PC
enum Voz { VOZ_NO, VOZ_ESCUCHA, VOZ_TRANSCRIBE, VOZ_CUENTA, VOZ_CLAUDE };
Voz voz = VOZ_NO;
bool vad_activo = false;   // grabando un trozo para ver si dice "Oye Clawd"
bool voz_auto = false;     // escuchando tras "Oye Clawd": se para sola al callarte
uint32_t voz_ultimo_sonido = 0;
uint32_t voz_desde = 0, cuenta_fin = 0, baila_hasta = 0;
bool ptt_tactil = false;   // la grabación la empezó un dedo (y no el botón)
const uint32_t VOZ_MAX_MS = 15000;

// ---------- La terminal: un historial de líneas de 29 letras ----------
const int TERM_COLS = 32, TERM_N = 120, TERM_FILAS = 14;
struct LineaTerm { char txt[TERM_COLS * 3 + 1]; uint16_t col; };   // UTF-8: hasta 3 bytes por letra
LineaTerm term[TERM_N];
int term_total = 0;        // líneas escritas desde el principio
int term_scroll = 0, term_scroll_ini = 0;
uint32_t term_actividad = 0;

// ---- UTF-8: "á" ocupa 2 bytes pero es 1 letra ----
int letras(const String &t) {
  int n = 0;
  for (int i = 0; i < (int)t.length(); i++) if ((t[i] & 0xC0) != 0x80) n++;
  return n;
}
int byte_de_letra(const String &t, int k) {      // dónde empieza la letra número k
  int n = 0;
  for (int i = 0; i < (int)t.length(); i++)
    if ((t[i] & 0xC0) != 0x80) { if (n == k) return i; n++; }
  return t.length();
}

// Columnas que ocupa un texto: los caracteres chinos (3 bytes en UTF-8,
// desde U+3000) ocupan 2, como en una terminal; el resto, 1.
static int cols_letra(uint8_t b) { return b >= 0xE3 ? 2 : 1; }
int ancho_cols(const String &t) {
  int n = 0;
  for (int i = 0; i < (int)t.length(); i++) if ((t[i] & 0xC0) != 0x80) n += cols_letra(t[i]);
  return n;
}
int byte_de_columna(const String &t, int cols) {   // dónde se pasa de 'cols' columnas
  int n = 0;
  for (int i = 0; i < (int)t.length(); i++) {
    if ((t[i] & 0xC0) == 0x80) continue;
    n += cols_letra(t[i]);
    if (n > cols) return i;
  }
  return t.length();
}

void term_linea(const String &l, uint16_t col) {
  LineaTerm &d = term[term_total % TERM_N];
  strlcpy(d.txt, l.c_str(), sizeof(d.txt));
  d.col = col;
  term_total++;
}

// Añade texto partiéndolo en líneas sin cortar palabras (si se puede).
// La primera línea lleva 'prefijo'; las siguientes, una sangría igual de larga.
void term_add(const String &texto, uint16_t col, const char *prefijo = "") {
  String pre = prefijo;
  String sangria = String("                ").substring(0, letras(pre));
  int ini = 0;
  bool primera = true;
  while (ini <= (int)texto.length()) {
    int fin = texto.indexOf('\n', ini);
    if (fin < 0) fin = texto.length();
    String parrafo = texto.substring(ini, fin);
    do {
      String cab = primera ? pre : sangria;
      primera = false;
      int ancho = TERM_COLS - letras(cab);
      int corte = parrafo.length();
      if (ancho_cols(parrafo) > ancho) {
        int limite = byte_de_columna(parrafo, ancho);
        corte = parrafo.lastIndexOf(' ', limite);
        if (corte <= 0 || limite - corte > 24) corte = limite;   // chino: sin espacios, se corta donde llegue
      }
      term_linea(cab + parrafo.substring(0, corte), col);
      parrafo = parrafo.substring(corte);
      parrafo.trim();
    } while (parrafo.length());
    ini = fin + 1;
  }
  term_scroll = 0;
  term_actividad = ahora;   // OJO: con millis() saldría un pelín "en el futuro" y la resta daría la vuelta
}

void aviso(const String &txt, uint32_t ms = 2200) {
  aviso_txt = txt;
  aviso_hasta = ahora + ms;
}

void interaccion() { ultima_interaccion = ahora; }

bool despertar(bool con_reaccion = true) {
  interaccion();
  if (!durmiendo) return false;
  durmiendo = false;
  if (con_reaccion) {
    reaccionar(R_DESPERTAR, 1600);
    SONAR(S_DESPERTAR);
  }
  return true;
}

// ---------- Voz: grabar y mandar al PC ----------
// La voz viaja por el USB en trozos de texto base64: {"a":"AAEC..."}
void voz_mandar_trozos(bool todo) {
  static int16_t trozo[480];                // 30 ms de audio
  static unsigned char b64[1400];
  for (int vueltas = 0; todo || vueltas < 4; vueltas++) {
    if (!todo && grab_cab - grab_cola < 480) return;
    int n = grab_sacar(trozo, 480);
    if (n == 0) return;
    size_t len = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &len, (const unsigned char *)trozo, n * 2);
    Serial.print("{\"a\":\"");
    Serial.write(b64, len);
    Serial.print("\"}\n");
  }
}

void voz_empezar(bool por_tactil) {
  if (voz == VOZ_ESCUCHA) return;
  if (mic_mudo) {
    aviso(T("Micro apagado: baja la cortina"));
    term_add(T("El micro está apagado (baja la cortina)"), ROJO, "! ");
    SONAR(S_ERROR);
    return;
  }
  if (!pc_conectado()) {
    aviso(T("Para oírte necesito el PC"));
    term_add(T("Sin PC: el companion no está en marcha"), ROJO, "! ");
    SONAR(S_ERROR);
    return;
  }
  despertar(false);
  if (vad_activo) { vad_activo = false; pc_enviar("voz_auto_cancelar"); }   // el trozo de "Oye Clawd", fuera
  SONAR(S_ESCUCHO);
  delay(80);                                // que el pitido no entre en la grabación
  grab_cola = grab_cab;                     // vaciar el búfer
  grabando = true;
  ptt_tactil = por_tactil;
  voz = VOZ_ESCUCHA;
  voz_desde = ahora;
  pc_enviar("voz_ini");
}

void voz_terminar() {
  if (voz != VOZ_ESCUCHA) return;
  grabando = false;
  if (ahora - voz_desde < 500) {            // demasiado corto: ha sido sin querer
    pc_enviar("voz_cancelar");
    voz = VOZ_NO;
    return;
  }
  voz_mandar_trozos(true);
  pc_enviar("voz_fin");
  voz = VOZ_TRANSCRIBE;
  voz_desde = ahora;
  SONAR(S_OIDO);
}

void cmd_local(const String &c);
void poner_mudo(bool mudo);
extern bool avisos_abierto, ajustes_abierto;
void ajustes_toque(int x, int y);
void poner_idioma(Idioma i);
void reloj_toque(int x, int y);
void permiso_toque(int x, int y);
void avisos_toque(int x, int y);
bool en_rueda(int x, int y);
void rueda_empezar(int x, int y);
void rueda_mover(int x, int y);
void rueda_soltar();
void temp_empezar(uint32_t segundos);
void temp_cancelar();
void ir_a_pantalla_n(int n, int dir);
extern float cortina_obj;
void dibujar_alerta();
String duracion(uint32_t ms);
void notificar(uint8_t tipo, const String &titulo, const String &sub);
void poner_mic(bool apagado);
void texto_centrado(LGFX_Sprite &g, const char *t, int y_px, int p, uint16_t col);

// ---------- La galaxia del vault (como el plugin Nebula de Obsidian) ----------
// El PC manda cuántas notas tiene cada carpeta: cada carpeta es un anillo
// y cada nota una estrella que gira (las de dentro, más deprisa).
const int GAL_MAX = 12;
int gal_notas[GAL_MAX];
int gal_n = 0;
int gal_total = 0;
uint32_t gal_desde = 0;     // cuándo se abrió la pantalla (para volver sola)

// Cambiar el estado de la voz SIN pisar una grabación en curso (si no, al soltar
// el dedo no se mandaría lo grabado y el micro se quedaría grabando para siempre)
void poner_voz(Voz v) {
  if (voz == VOZ_ESCUCHA) return;
  voz = v;
  voz_desde = millis();
}

// Mensajes del PC (los define pc.h, llegan como JSON)
void al_mensaje(JsonDocument &doc) {
  if (doc["oido"].is<const char *>()) {           // lo que ha entendido Whisper
    term_add(doc["oido"].as<String>(), TXT2, "> ");
    int cuenta = doc["cuenta"] | 0;
    poner_voz(cuenta > 0 ? VOZ_CUENTA : VOZ_NO);
    cuenta_fin = millis() + cuenta * 1000;
    if (!(doc["mascota"] | 0)) pantalla = PANT_TERMINAL;   // las órdenes a Clawd se ven en él
  }
  if (doc["paso"].is<const char *>()) {           // Claude usa una herramienta
    term_add(doc["paso"].as<String>(), TXT3, "  └ ");
    poner_voz(VOZ_CLAUDE);
  }
  if (doc["resp"].is<const char *>()) {           // la respuesta de Claude
    term_add(doc["resp"].as<String>(), TXT, "● ");
    term_add("", CREMA);
    poner_voz(VOZ_NO);
  }
  if (doc["local"].is<const char *>()) {          // un comando que hace Clawd solo
    term_add(doc["local"].as<String>(), TXT2, "● ");
    poner_voz(VOZ_NO);
  }
  if (doc["err"].is<const char *>()) {
    term_add(doc["err"].as<String>(), ROJO, "! ");
    poner_voz(VOZ_NO);
    SONAR(S_ERROR);
  }
  if (doc["galaxia"].is<JsonArray>()) {           // notas por carpeta del vault
    gal_n = 0;
    for (JsonVariant v : doc["galaxia"].as<JsonArray>()) {
      if (gal_n >= GAL_MAX) break;
      gal_notas[gal_n++] = v.as<int>();
    }
    gal_total = doc["notas"] | 0;
  }
  if (doc["proyecto"].is<const char *>()) mon_proyecto = doc["proyecto"].as<String>();
  if (doc["act"].is<const char *>()) {             // un paso de Claude en el PC
    for (int i = 0; i < MON_ACTS - 1; i++) mon_acts[i] = mon_acts[i + 1];
    mon_acts[MON_ACTS - 1] = doc["act"].as<String>();
  }
  if (doc["tareas"].is<JsonArray>()) {             // su lista de tareas
    mon_n = 0;
    for (JsonArray t : doc["tareas"].as<JsonArray>()) {
      if (mon_n >= 12) break;
      mon_tareas[mon_n].estado = t[0] | 0;
      strlcpy(mon_tareas[mon_n].txt, t[1] | "", sizeof(mon_tareas[mon_n].txt));
      mon_n++;
    }
  }
  if (doc["resumen"].is<const char *>()) mon_resumen = doc["resumen"].as<String>();
  if (doc["v"].is<const char *>()) {                // un trozo de la voz de Clawd
    static int16_t m[700];
    size_t len = 0;
    const char *b = doc["v"];
    if (mbedtls_base64_decode((unsigned char *)m, sizeof(m), &len, (const unsigned char *)b, strlen(b)) == 0)
      rep_meter(m, len / 2);
  }
  if (doc["callar"].is<int>()) rep_callar();
  if (doc["aviso"].is<const char *>() && doc["aviso"].as<String>() == "listo") {   // algo largo ha acabado
    claude_duro = (doc["dur"] | 0) * 1000UL;
    claude_fin = millis();
    if (alerta != AL_TIEMPO) alerta = AL_LISTO;   // no apagar una alarma que está sonando
    despertar(false);
    notificar(0, T("Claude ha terminado"), (mon_proyecto.length() ? mon_proyecto : String("Claude Code")) + " · " + duracion(claude_duro));
    reaccionar(R_LISTO, 3500);
    animar(A_CELEBRATE, ahora);
    SONAR(S_LISTO);                                // una vez
    soltar_confeti(cx, SUELO - 90, 30);
  }
  if (doc["permiso"].is<JsonObject>() &&
      !(perm_activo && perm_id == doc["permiso"]["id"].as<String>())) {   // (el PC lo reenvía: si ya lo tengo, nada)
    JsonObject p = doc["permiso"];
    perm_id = p["id"] | "";
    perm_herr = p["herramienta"] | "";
    perm_det = p["detalle"] | "";
    perm_proy = p["proyecto"] | "";
    perm_activo = true;
    perm_desde = millis();
    perm_prox_pitido = 0;
    despertar(false);
    notificar(1, T("Claude pide permiso"), perm_herr + " · " + perm_proy);
  }
  if (doc["permiso_fin"].is<const char *>() && perm_id == doc["permiso_fin"].as<String>()) perm_activo = false;
  if (doc["cmd"].is<const char *>()) cmd_local(doc["cmd"].as<String>());
}

// ---------- LED de colores ----------
// led() solo dice A QUÉ color ir; led_aplicar() se acerca poco a poco (fundido).
float led_obj[3] = {0, 0, 0}, led_act[3] = {0, 0, 0};
void led(uint8_t r, uint8_t gr, uint8_t b) { led_obj[0] = r; led_obj[1] = gr; led_obj[2] = b; }
void led_pulso(float periodo_ms, uint8_t r, uint8_t gr, uint8_t b) {   // late suave
  float k = 0.15f + 0.85f * (0.5f + 0.5f * sinf(ahora * 6.2832f / periodo_ms));
  led(r * k, gr * k, b * k);
}
void led_aplicar() {
  static uint32_t ultimo = 0xFFFFFFFF;
  for (int i = 0; i < 3; i++) led_act[i] += (led_obj[i] - led_act[i]) * 0.25f;
  uint8_t r = lroundf(led_act[0]), gr = lroundf(led_act[1]), b = lroundf(led_act[2]);
  uint32_t v = (r << 16) | (gr << 8) | b;
  if (v == ultimo) return;
  ultimo = v;
  rgbLedWrite(PIN_RGB, r, gr, b);
}

// ============================================================
//  1. LEER
// ============================================================
bool sobre_clawd(int x, int y) {
  return abs(x - (int)cx) < 72 && y > SUELO - 100 - (int)alto && y < SUELO + 16;
}

void al_tocar(int x, int y) {
  pc_enviar("toque");
  bool estaba_dormido = durmiendo;
  if (despertar()) return;
  (void)estaba_dormido;

  if (recordatorio >= 0) {                       // "vale, ya bebo agua"
    recordatorio = -1;
    reaccionar(R_SALTO, 700);
    saltar(7);
    SONAR(S_SALTO);
    aviso(T("Bien hecho!"));
    return;
  }
  if (y > PANEL_Y) {                             // panel: siguiente página
    pagina = (pagina + 1) % 3;
    prox_pagina = ahora + 12000;
    SONAR(S_CLIC);
    return;
  }
  if (sobre_clawd(x, y)) {
    if (ahora - ultimo_tap < 400) {              // doble toque: pomodoro
      if (pomo == POMO_NO) {
        pomo = POMO_FOCO;
        pomo_ini = ahora;
        pomo_fin = ahora + FOCO_MS;
        SONAR(S_POMO_INI);
        aviso(T("Pomodoro: 25 min de foco"));
      } else {
        pomo = POMO_NO;
        aviso(T("Pomodoro cancelado"));
      }
      ultimo_tap = 0;
      return;
    }
    ultimo_tap = ahora;
    animar(anim_al_azar(true), ahora);           // como al hacer clic en Claude Code
    SONAR(S_SALTO);
    return;
  }
  // el suelo vacío: "ven aquí"
  objetivo_x = constrain(x, 70, 170);
  andando = true;
  prox_accion = ahora + 5000;
}

void empezar_pomodoro() {
  pomo = POMO_FOCO;
  pomo_ini = ahora;
  pomo_fin = ahora + FOCO_MS;
  SONAR(S_POMO_INI);
  aviso(T("Pomodoro: 25 min de foco"));
}

String mmss(uint32_t ms);
void ir_a_pantalla_n(int n, int dir);

// Órdenes que Clawd hace él solo (el PC las reconoce en lo que dices)
void cmd_local(const String &c) {
  interaccion();
  if (c == "pomodoro")       { despertar(false); empezar_pomodoro(); }
  else if (c == "baila")     { despertar(false); baila_hasta = ahora + 12000; }
  else if (c == "dormir")    { durmiendo = true; andando = false; SONAR(S_DORMIR); }
  else if (c == "despertar") { despertar(); }
  else if (c == "mudo")      { poner_mudo(true); }
  else if (c == "sonido")    { poner_mudo(false); }
  else if (c == "salta")     { despertar(false); reaccionar(R_SALTO, 700); saltar(9); SONAR(S_SALTO); }
  else if (c == "mascota")   { pantalla = PANT_MASCOTA; }
  else if (c.startsWith("anim:")) { despertar(false); animar(anim_por_nombre(c.substring(5).c_str()), ahora); }
  else if (c == "claude")    { pantalla = PANT_CLAUDE; }
  else if (c == "captura")   { pedir_captura = true; }

  else if (c.startsWith("pantalla:")) { int n = constrain(c.substring(9).toInt(), 0, 4); ir_a_pantalla_n(n, n > (int)pantalla ? 1 : -1); }
  else if (c == "cortina")   { cortina_obj = cortina_obj > 0.5f ? 0 : 1; }
  else if (c == "escuchar")  { if (voz == VOZ_NO) { voz_empezar(false); voz_auto = true; voz_ultimo_sonido = ahora; } }
  else if (c == "reloj")     { reloj_abierto = true; cortina_obj = 0; rueda_modo = 0; }
  else if (c.startsWith("prueba:rueda:")) {      // "prueba:rueda:0:5100" = horas, 1 h 25 min
    rueda_modo = c.substring(13, 14).toInt();
    temp_dur = c.substring(15).toInt();
    reloj_abierto = true;
  }
  else if (c.startsWith("temporizador:")) {
    uint32_t sg = c.substring(13).toInt();
    if (sg == 0) temp_cancelar(); else temp_empezar(sg);
  }
  else if (c == "prueba:listo") { mon_resumen = "He terminado el rediseño: barra de estado, cortina y transiciones. Todo compila."; claude_duro = 754000; claude_fin = ahora; alerta = AL_LISTO; notificar(0, T("Claude ha terminado"), "clawd · 12 min"); }
  else if (c == "prueba:espera") { alerta = AL_ESPERA; }
  else if (c == "prueba:susto") { reaccionar(R_SUSTO, 1500); saltar(10); }
  else if (c == "prueba:jugar") { fase = F_JUGANDO; }
  else if (c == "avisos")    { avisos_abierto = !avisos_abierto; }
  else if (c == "ajustes")   { ajustes_abierto = !ajustes_abierto; }
  else if (c.startsWith("idioma:")) {
    String cod = c.substring(7);
    for (int i = 0; i < 4; i++) if (cod == CODIGO_IDIOMA[i]) poner_idioma((Idioma)i);
  }
  else if (c == "prueba:nada") { alerta = AL_NO; }
  else if (c == "juegos")    { pantalla = PANT_JUEGOS; juego = J_MENU; fase = F_LISTO; }
  else if (c == "vuela")     { pantalla = PANT_JUEGOS; juego_empezar(J_VUELA); }
  else if (c == "corre")     { pantalla = PANT_JUEGOS; juego_empezar(J_CORRE); }
  else if (c == "terminal")  { pantalla = PANT_TERMINAL; term_actividad = ahora; }
  else if (c == "galaxia")   { pantalla = PANT_GALAXIA; gal_desde = ahora; }
}


// Toque corto en la pantalla de la terminal
void al_tocar_terminal(int x, int y) {
  (void)x;
  term_actividad = ahora;
  if (voz == VOZ_CUENTA || voz == VOZ_CLAUDE || pc_claude_voz) {   // cancelar
    pc_enviar("cancelar");
    term_add(T("Cancelado"), ROJO, "! ");
    voz = VOZ_NO;
    return;
  }
  if (y < 64) { pantalla = PANT_MASCOTA; SONAR(S_CLIC); }          // cabecera: volver
}

void poner_mic(bool apagado) {
  mic_mudo = apagado;
  ajustes.putBool("mic", apagado);
  if (apagado) { musica_desde = 0; baila_hasta = 0; }
  SONAR(S_CLIC);
  aviso(apagado ? T("Micro: apagado") : T("Micro: encendido"));
}

void poner_mudo(bool mudo) {
  audio_mudo = mudo;
  ajustes.putBool("mudo", mudo);
  if (!mudo) SONAR(S_CLIC);
  aviso(mudo ? T("Sonido: apagado") : T("Sonido: encendido"));
}

// ---------- Gestos ----------
// En cuanto el dedo se mueve más de 14 px, se decide QUÉ gesto es y ya no cambia:
//   - horizontal                   -> DESLIZAR (cambia de pantalla, en el acto)
//   - vertical hacia abajo         -> CORTINA (desde arriba en cualquier pantalla)
//   - vertical en la terminal      -> ARRASTRAR (desplaza el historial)
//   - si no se mueve en 600 ms     -> MANTENER (hablar o mimos)
//   - si se suelta antes, quieto   -> TOQUE
enum Gesto { G_NADA, G_DUDA, G_DESLIZAR, G_ARRASTRAR, G_MANTENER, G_CORTINA, G_BRILLO, G_RUEDA, G_VOLUMEN };
Gesto gesto = G_NADA;
uint32_t fallos_tactil = 0;                 // lecturas seguidas sin dedo
uint32_t indicador_hasta = 0;               // puntitos de "en qué pantalla estás"
const int N_PANTALLAS = 5;
const int DOCK_W = 240 / N_PANTALLAS;       // ancho de cada icono del dock de la cortina
const int BARRA_H = 24;                     // la barra de estado de arriba

// ---------- Transición entre pantallas: el contenido se desliza ----------
LGFX_Sprite previa(&tft);                   // el último fotograma de la pantalla que se va
bool hay_previa = false;
uint32_t trans_ini = 0;
int trans_dir = 0;
const uint32_t TRANS_MS = 260;

void ir_a_pantalla_n(int n, int dir) {
  if (n == (int)pantalla) return;
  if (hay_previa) {                         // guardar cómo estaba, para deslizarlo fuera
    memcpy(previa.getBuffer(), g.getBuffer(), 240 * 320 * 2);
    trans_ini = ahora;
    trans_dir = dir;
  }
  pantalla = (Pantalla)n;
  if (pantalla == PANT_JUEGOS) { juego = J_MENU; fase = F_LISTO; }
  if (pantalla == PANT_GALAXIA) gal_desde = ahora;
  term_actividad = ahora;
  term_scroll = 0;
  indicador_hasta = ahora + 1200;
}

void ir_a_pantalla(int delta) {
  ir_a_pantalla_n(((int)pantalla + delta + N_PANTALLAS) % N_PANTALLAS, delta > 0 ? 1 : -1);
  SONAR(S_CLIC);
}

void dibujar_indicador() {                  // puntitos en la barra: en qué pantalla estás
  if (ahora > indicador_hasta) return;
  int x0 = 120 - (N_PANTALLAS * 10 - 6) / 2;
  for (int i = 0; i < N_PANTALLAS; i++)
    g.fillRect(x0 + i * 10, 10, 4, 4, i == (int)pantalla ? TXT : TXT3);
}

// ---------- Ajustes de la cortina (se guardan en la placa) ----------
bool habla_on = true;      // Clawd dice en voz alta las respuestas y los avisos
bool oye_on = true;        // "Oye Clawd": escucha si le llamas
bool carteles_on = false;  // los carteles de cada 50 min (beber agua...): apagados de serie
int volumen = 70;          // % del altavoz

// ---------- "Oye Clawd": la placa nota que hablas y le pasa el audio al PC ----------
uint32_t vad_desde = 0, vad_ultimo_sonido = 0, vad_pausa_hasta = 0;

// ---------- La cortina: se baja desde arriba, como en un móvil ----------
float cortina = 0, cortina_obj = 0, cortina_ini = 0;   // 0 cerrada ... 1 abierta
bool cortina_arrastre = false;
int brillo_usuario = 100;                   // % que eliges en la cortina (se guarda)
const int CT_FILA1 = 52, CT_FILA2 = 114, CT_BOT = 40, CT_BRILLO = 184, CT_VOLUMEN = 206, CT_AVISO = 226, CT_DOCK = 264;
bool avisos_abierto = false;               // la lista completa de avisos
bool ajustes_abierto = false;              // Ajustes (el idioma)

// Historial de avisos (lo último, primero)
struct Notif { uint8_t tipo; String titulo, sub; uint32_t t; };   // 0 listo, 1 espera, 2 pomodoro
const int MAX_NOTIFS = 6;
Notif notifs[MAX_NOTIFS];
int n_notifs = 0;

void notificar(uint8_t tipo, const String &titulo, const String &sub) {
  for (int i = MAX_NOTIFS - 1; i > 0; i--) notifs[i] = notifs[i - 1];
  notifs[0] = {tipo, titulo, sub, ahora};
  n_notifs = min(n_notifs + 1, MAX_NOTIFS);
}

// "Habla" de verdad = encendido y sin "No molestar": se lo dice al PC
void enviar_habla() { pc_enviar(habla_on && !no_molestar ? "habla:1" : "habla:0"); }

void poner_ajuste(const char *clave, bool &var, bool valor, const char *si, const char *no) {
  var = valor;
  ajustes.putBool(clave, valor);
  aviso(valor ? si : no);
}

void empezar_pomodoro();
void cmd_local(const String &c);

bool en_brillo(int y) { return y >= CT_BRILLO - 10 && y < CT_BRILLO + 11; }
bool en_volumen(int y) { return y >= CT_VOLUMEN - 10 && y < CT_VOLUMEN + 12; }
void brillo_desde_x(int x) { brillo_usuario = constrain((x - 40) * 100 / 180, 10, 100); }
void volumen_desde_x(int x) {
  int v = constrain((x - 40) * 100 / 180, 0, 100);
  if (v / 5 != volumen / 5) SONAR(S_TIC);   // un "tic" cada 5 %
  volumen = v;
  audio_volumen(volumen);
}

void cortina_toque(int x, int y) {
  SONAR(S_CLIC);
  if (y < 46 && x > 196) { ajustes_abierto = true; cortina_obj = 0; return; }   // ⚙ Ajustes
  int fila = (y >= CT_FILA1 && y < CT_FILA1 + 58) ? 0 : (y >= CT_FILA2 && y < CT_FILA2 + 58) ? 1 : -1;
  if (fila >= 0) {
    int i = fila * 4 + constrain(x / 60, 0, 3);
    switch (i) {
      case 0: poner_mudo(!audio_mudo); break;
      case 1: poner_mic(!mic_mudo); break;
      case 2:
        no_molestar = !no_molestar;
        ajustes.putBool("dnd", no_molestar);
        aviso(no_molestar ? T("No molestar: solo alarmas y permisos") : T("No molestar: apagado"));
        enviar_habla();
        break;
      case 3: if (durmiendo) despertar(); else cmd_local("dormir"); break;
      case 4: reloj_abierto = true; rueda_modo = 0; cortina_obj = 0; break;
      case 5: poner_ajuste("oye", oye_on, !oye_on, T("Oye Clawd: escuchando"), T("Oye Clawd: apagado")); break;
      case 6: poner_ajuste("habla", habla_on, !habla_on, T("Clawd habla"), T("Clawd no habla")); enviar_habla(); break;
      case 7: poner_ajuste("carteles", carteles_on, !carteles_on, T("Carteles: activados"), T("Carteles: apagados")); break;
    }
  } else if (y >= CT_AVISO && y < CT_DOCK - 2) {
    if (n_notifs > 0) { avisos_abierto = true; cortina_obj = 0; }
  } else if (y >= CT_DOCK && y < 308) {
    int n = constrain(x / DOCK_W, 0, N_PANTALLAS - 1);
    ir_a_pantalla_n(n, n > (int)pantalla ? 1 : -1);
    cortina_obj = 0;
  } else if (y >= 308) {
    cortina_obj = 0;                        // el asa de abajo: cerrar
  }
}

void leer_tactil() {
  int16_t x = 0, y = 0;
  bool lectura = tactil_leer(x, y);
  // el táctil a veces "pierde" el dedo un instante: soltar solo tras 2 lecturas sin él
  if (lectura) fallos_tactil = 0; else fallos_tactil++;
  bool toca = lectura || (tocaba && fallos_tactil < 2);
  static int16_t ux = 0, uy = 0;            // última posición buena del dedo
  if (lectura) { ux = x; uy = y; }
  const bool abierta = cortina_obj > 0.5f;
  const bool jugando = pantalla == PANT_JUEGOS && fase == F_JUGANDO && juego != J_MENU;

  if (toca && !tocaba) {                    // ---- apoya el dedo ----
    toque_ini = ahora;
    tx0 = ux; ty0 = uy;
    gesto = G_DUDA;
    cortina_ini = cortina;
    toque_permiso = perm_activo && uy >= BARRA_H;               // el permiso, antes que nada
    toque_aviso = !toque_permiso && alerta != AL_NO && !abierta && uy >= BARRA_H;   // el aviso se queda este toque
    toque_en_clawd = !abierta && pantalla == PANT_MASCOTA && sobre_clawd(ux, uy) && !durmiendo;
    term_scroll_ini = term_scroll;
    if (abierta && en_brillo(uy)) { gesto = G_BRILLO; brillo_desde_x(ux); }
    else if (abierta && en_volumen(uy)) { gesto = G_VOLUMEN; volumen_desde_x(ux); }
    // la rueda del temporizador: se agarra en cuanto apoyas el dedo en el anillo
    if (!abierta && reloj_abierto && temp == T_PARADO && en_rueda(ux, uy)) { gesto = G_RUEDA; rueda_empezar(ux, uy); }
    // en los juegos se reacciona al APOYAR el dedo (no al soltarlo): más rápido
    if (!abierta && !reloj_abierto && !perm_activo && pantalla == PANT_JUEGOS && !toque_aviso && uy >= 48) {
      juego_toque(ux, uy, ahora);
      interaccion();
    }
  }

  if (toque_permiso) {                      // el toque era para contestar el permiso
    if (!toca && tocaba) {
      toque_permiso = false;
      gesto = G_NADA;
      permiso_toque(tx0, ty0);
    }
    tocaba = toca;
    return;
  }

  if (toque_aviso) {                        // el toque era para cerrar el aviso
    if (!toca && tocaba) {
      if (alerta == AL_TIEMPO) temp = T_PARADO;   // para la alarma
      alerta = AL_NO;
      toque_aviso = false;
      pc_enviar("visto");
      SONAR(S_CLIC);
      gesto = G_NADA;
    }
    tocaba = toca;
    return;
  }

  if (toca) {                               // ---- el dedo sigue ahí ----
    int dx = ux - tx0, dy = uy - ty0;
    uint32_t dura = ahora - toque_ini;
    if (gesto == G_BRILLO) brillo_desde_x(ux);
    if (gesto == G_VOLUMEN) volumen_desde_x(ux);
    if (gesto == G_RUEDA) rueda_mover(ux, uy);
    if (gesto == G_DUDA && (abs(dx) > 14 || abs(dy) > 14)) {
      if (abs(dy) > abs(dx)) {
        bool desde_arriba = ty0 < 48;
        bool abre = !abierta && dy > 0 && !jugando &&
                    (desde_arriba || pantalla == PANT_MASCOTA || pantalla == PANT_GALAXIA);
        bool cierra = abierta && dy < 0;
        gesto = (abre || cierra) ? G_CORTINA : (abierta ? G_NADA : G_ARRASTRAR);
      } else {
        gesto = (abierta || reloj_abierto || avisos_abierto || ajustes_abierto) ? G_NADA : G_DESLIZAR;
      }
    }
    if (gesto == G_CORTINA) {               // la cortina sigue al dedo
      cortina_arrastre = true;
      cortina = constrain(cortina_ini + dy / 240.0f, 0.0f, 1.0f);
    }
    if (gesto == G_DESLIZAR && abs(dx) > 55 && !jugando) {
      ir_a_pantalla(dx < 0 ? 1 : -1);       // hacia la izquierda = siguiente, como en el móvil
      gesto = G_NADA;
    }
    if (gesto == G_ARRASTRAR && pantalla == PANT_TERMINAL) {
      int max_scroll = max(0, min(term_total, TERM_N) - TERM_FILAS);
      term_scroll = constrain(term_scroll_ini + dy / 16, 0, max_scroll);
      term_actividad = ahora;
    }
    if (gesto == G_DUDA && dura > 600 && !abierta && !reloj_abierto) {   // quieto 0,6 s: mantener
      gesto = G_MANTENER;
      bool zona_hablar = pantalla == PANT_MASCOTA ? ty0 > PANEL_Y
                       : (pantalla == PANT_TERMINAL && ty0 > 64);
      if (toque_en_clawd) {
        mimando = true;
        interaccion();
        SONAR(S_MIMOS);
        pc_enviar("mimos");
      } else if (zona_hablar) {
        voz_empezar(true);
      }
    }
  }

  if (!toca && tocaba) {                    // ---- suelta el dedo ----
    if (gesto == G_CORTINA) {
      cortina_arrastre = false;
      // basta con bajarla un poco para abrirla, y subirla un poco para cerrarla
      cortina_obj = cortina_ini > 0.5f ? (cortina < 0.8f ? 0 : 1) : (cortina > 0.2f ? 1 : 0);
      interaccion();
    } else if (gesto == G_BRILLO) {
      ajustes.putInt("brillo", brillo_usuario);
    } else if (gesto == G_VOLUMEN) {
      ajustes.putInt("volumen", volumen);
    } else if (gesto == G_RUEDA) {
      rueda_soltar();
    } else if (voz == VOZ_ESCUCHA && ptt_tactil) {
      voz_terminar();
    } else if (mimando) {
      mimando = false;
      reaccionar(R_CONTENTO, 1200);
    } else if (gesto == G_DUDA) {           // no se movió ni se mantuvo: un toque
      if (abierta) cortina_toque(tx0, ty0);
      else if (avisos_abierto) avisos_toque(tx0, ty0);
      else if (ajustes_abierto) ajustes_toque(tx0, ty0);
      else if (reloj_abierto && ty0 >= BARRA_H) reloj_toque(tx0, ty0);
      else if (ty0 < BARRA_H) { cortina_obj = 1; SONAR(S_CLIC); }   // tocar la barra: abre la cortina
      else if (pantalla == PANT_MASCOTA) al_tocar(tx0, ty0);
      else if (pantalla != PANT_JUEGOS) al_tocar_terminal(tx0, ty0);
    }
    gesto = G_NADA;
  }
  if (mimando) interaccion();
  tocaba = toca;
}

// El botón BOOT de la placa también sirve para hablar (mantenlo pulsado)
void leer_boton() {
  static bool antes = false;
  bool pulsado = digitalRead(0) == LOW;
  if (pulsado && !antes) voz_empezar(false);
  if (!pulsado && antes && !ptt_tactil) voz_terminar();
  antes = pulsado;
}

void actualizar_voz() {
  if (voz == VOZ_ESCUCHA) {
    voz_mandar_trozos(false);
    if (ahora - voz_desde > VOZ_MAX_MS) voz_terminar();   // máximo 15 s
    // tras "Oye Clawd", se para sola cuando te callas (1 s de silencio)
    if (voz_auto && ahora - voz_desde > 1500 && ahora - voz_ultimo_sonido > 1000) voz_terminar();
  }
  if (voz != VOZ_ESCUCHA) voz_auto = false;
  // si el PC no contesta, no quedarse colgado
  if (voz == VOZ_TRANSCRIBE && ahora - voz_desde > 30000) {
    voz = VOZ_NO;
    term_add(T("El PC no ha contestado"), ROJO, "! ");
  }
  if (voz == VOZ_CUENTA && ahora > cuenta_fin + 3000 && !pc_claude_voz) voz = VOZ_NO;
  if (voz == VOZ_CLAUDE && (!pc_conectado() || (!pc_claude_voz && ahora - voz_desde > 4000))) voz = VOZ_NO;
  if (voz != VOZ_NO || pc_claude_voz) { term_actividad = ahora; interaccion(); }
  // volver a la mascota tras 40 s sin nada nuevo en la terminal
  if (pantalla == PANT_TERMINAL && !tocaba && (int32_t)(ahora - term_actividad) > 40000) pantalla = PANT_MASCOTA;
  // la galaxia se queda 2 minutos
  if (pantalla == PANT_GALAXIA && !tocaba && voz == VOZ_NO && ahora - gal_desde > 120000) pantalla = PANT_MASCOTA;
}

void leer_micro() {
  float r = mic_pico;
  mic_pico = 0;
  if (ahora < 3000) return;                      // el micro tarda en estabilizarse
  if (mic_mudo) {
    musica_desde = 0;
    bailando = ahora < baila_hasta;
    if (vad_activo) { vad_activo = false; grabando = false; pc_enviar("voz_auto_cancelar"); }
    return;
  }
  if (voz == VOZ_ESCUCHA) {
    musica_desde = 0;
    nivel_voz += (r - nivel_voz) * 0.3f;
    if (r > max(110.0f, min(suelo_ruido, 30.0f) * 5)) voz_ultimo_sonido = ahora;
    return;
  }

  // "Oye Clawd": si nota voz, graba ese trozo (con el medio segundo de antes) y lo manda al PC.
  // El PC lo pasa a texto y solo hace caso si empieza por "Oye Clawd"; si no, lo tira.
  const float umbral_voz = max(110.0f, min(suelo_ruido, 30.0f) * 5);
  bool puede = oye_on && !mic_mudo && !no_molestar && voz == VOZ_NO && !bailando && !durmiendo &&
               pc_conectado() && rep_nivel == 0 && ahora > vad_pausa_hasta && pantalla != PANT_JUEGOS;
  if (!vad_activo && puede && r > umbral_voz) {
    vad_activo = true;
    vad_desde = vad_ultimo_sonido = ahora;
    grab_con_previo();
    pc_enviar("voz_auto_ini");
  }
  if (vad_activo) {
    if (r > umbral_voz) vad_ultimo_sonido = ahora;
    voz_mandar_trozos(false);
    bool fin = ahora - vad_ultimo_sonido > 700 || ahora - vad_desde > 6000 || !puede;
    if (fin) {
      grabando = false;
      voz_mandar_trozos(true);
      pc_enviar("voz_auto_fin");
      vad_activo = false;
      vad_pausa_hasta = ahora + 400;
    }
    return;                                  // hablar no es ni música ni palmada
  }   // hablando no cuenta como ruido
  if (r > 0) suelo_ruido += (r < suelo_ruido ? 0.05f : 0.002f) * (r - suelo_ruido);
  ruido_suave += (r - ruido_suave) * 0.08f;

  // música: ruido medio y constante durante 3 s
  // Medido: silencio ~10, hablar ~20, música 75-125, palmada 900-3900.
  // El suelo se limita a 30 para que la música larga no suba el umbral.
  float umbral_musica = max(70.0f, min(suelo_ruido, 30.0f) * 2.0f);
  if (ruido_suave > umbral_musica) {
    if (!musica_desde) musica_desde = ahora;
  } else if (ruido_suave < umbral_musica * 0.6f) {
    musica_desde = 0;
  }
  bool bailaba = bailando;
  bailando = (musica_desde && ahora - musica_desde > 2500) || ahora < baila_hasta;
  if (bailando) interaccion();
  if (bailando && !bailaba) pc_enviar("musica");

  // cada 2 s, mandar al PC cómo está el micro (para ajustar los umbrales)
  static uint32_t prox_info = 0;
  static float pico_info = 0;
  pico_info = max(pico_info, r);
  if (ahora > prox_info) {
    prox_info = ahora + 2000;
    char b[96];
    snprintf(b, sizeof(b), "{\"ev\":\"mic\",\"suelo\":%d,\"suave\":%d,\"pico\":%d,\"baila\":%d}",
             (int)suelo_ruido, (int)ruido_suave, (int)pico_info, bailando ? 1 : 0);
    pc_enviar_linea(b);
    pico_info = 0;
  }

  // palmada: un pico muy por encima de lo normal
  float umbral_palmada = max(max(600.0f, min(suelo_ruido, 30.0f) * 8), bailando ? ruido_suave * 4 : 0.0f);
  if (r > umbral_palmada && ahora - ultima_palmada > 1500) {
    ultima_palmada = ahora;
    pc_enviar("palmada");
    if (despertar()) return;
    reaccionar(R_SUSTO, 1500);
    saltar(10);
    SONAR(S_SUSTO);
  }
}

void leer_pc() {
  pc_actualizar();
  bool conectado = pc_conectado();
  if (conectado && !pc_estaba && !durmiendo && reac == R_NADA) animar(A_WADDLE, ahora);   // entra andando de lado
  pc_estaba = conectado;
  if (conectado && !arranque_contado) {     // contarle al PC por qué hemos arrancado
    arranque_contado = true;
    char b[160];
    snprintf(b, sizeof(b), "{\"ev\":\"arranque\",\"motivo\":\"%s\",\"paso\":%lu,\"habla\":%d,\"idioma\":\"%s\"}",
             nombre_motivo(motivo_arranque), (unsigned long)miga_anterior, habla_on && !no_molestar ? 1 : 0,
             CODIGO_IDIOMA[idioma]);
    pc_enviar_linea(b);
  }

  // Claude Code: lo que hace se ve (Clawd, el puntito de la barra), pero NO suena.
  // Los avisos de verdad los decide el PC y llegan como {"aviso":"listo"} (ver al_mensaje).
  String c = conectado ? pc_claude : "";
  if (c != claude_prev) {
    if (c == "trabajando" && claude_prev != "esperando") claude_desde = ahora;
    if (c == "trabajando") { if (alerta == AL_LISTO) alerta = AL_NO; mon_resumen = ""; despertar(false); }
    // con la tapa cerrada, enseñar el progreso en cuanto empieza a trabajar
    if ((c == "trabajando" || c == "esperando") && pc_tapa && pantalla == PANT_MASCOTA) pantalla = PANT_CLAUDE;
    if (c == "terminado" && (claude_prev == "trabajando" || claude_prev == "esperando")) {
      claude_duro = ahora - claude_desde;
      claude_fin = ahora;
      if (!durmiendo) animar(A_CELEBRATE, ahora);   // una celebración pequeña y en silencio
    }
    claude_prev = c;
  }
  if (c == "trabajando" || c == "esperando") interaccion();
  // al cerrar la tapa con Claude trabajando: a la pantalla de progreso
  static bool tapa_antes = false;
  if (pc_tapa && !tapa_antes && (c == "trabajando" || c == "esperando")) pantalla = PANT_CLAUDE;
  tapa_antes = pc_tapa;
}

// ============================================================
//  2. DECIDIR (y mover)
// ============================================================
void actualizar_tiempos() {
  // pomodoro
  if (pomo == POMO_FOCO && ahora > pomo_fin) {
    pomo = POMO_DESCANSO;
    pomo_ini = ahora;
    pomo_fin = ahora + DESCANSO_MS;
    despertar(false);
    reaccionar(R_POMO_FIN, 3500);
    notificar(2, T("Pomodoro terminado"), T("Toca descansar 5 minutos"));
    animar(A_CELEBRATE, ahora);
    saltar(9);
    SONAR(S_POMO_FIN);
    soltar_confeti(cx, SUELO - 90, 25);
    aviso(T("Descanso: 5 minutos"), 4000);
  } else if (pomo == POMO_DESCANSO && ahora > pomo_fin) {
    pomo = POMO_NO;
    despertar(false);
    reaccionar(R_DESCANSO_FIN, 2500);
    saltar(7);
    SONAR(S_RECORDAR);
  }

  // recordatorios (no durante el pomodoro, que ya tiene sus descansos)
  if (ahora > prox_recordatorio) {
    prox_recordatorio = ahora + 50UL * 60 * 1000;
    if (carteles_on && !no_molestar && !durmiendo && pomo == POMO_NO && pc_conectado()) {
      recordatorio = random(sizeof(RECORDATORIOS) / sizeof(RECORDATORIOS[0]));
      rec_hasta = ahora + 90000;
      SONAR(S_RECORDAR);
    }
  }
  if (recordatorio >= 0 && ahora > rec_hasta) recordatorio = -1;

  if (reac != R_NADA && ahora > reac_hasta) reac = R_NADA;

  // ¿a dormir?
  int h = hora_h();
  bool noche = h >= 0 && h < 7;
  bool claude_activo = claude_prev == "trabajando" || claude_prev == "esperando";
  uint32_t paciencia = noche ? 60000 : 5UL * 60 * 1000;
  if (!durmiendo && !claude_activo && pomo != POMO_FOCO && reac == R_NADA && recordatorio < 0 &&
      ahora - ultima_interaccion > paciencia) {
    durmiendo = true;
    andando = false;                       // se duerme en silencio: no ha pasado nada que contar
  }
}

void mover_cuerpo(bool libre, float velocidad) {
  // física del salto
  if (alto > 0 || vy < 0) {
    alto -= vy;
    vy += 0.9f;
    if (alto <= 0) {
      alto = 0;
      vy = 0;
      aplastado_hasta = ahora + 140;   // al caer, se aplasta un momento
    }
  }

  // pasear él solo
  if (libre && ahora > prox_accion && !andando) {
    int r = random(100);
    if (r < 45) {
      objetivo_x = random(70, 171);
      andando = true;
    } else if (r < 62) {
      animar(anim_al_azar(true), ahora);         // de vez en cuando, una de Claude Code
    }
    prox_accion = ahora + random(2500, 7000);
  }
  if (andando) {
    float d = objetivo_x - cx;
    if (fabsf(d) <= velocidad) {
      cx = objetivo_x;
      andando = false;
    } else {
      dir = d > 0 ? 1 : -1;
      cx += dir * velocidad;
    }
  }

  // parpadear y mirar alrededor
  if (ahora > prox_parpadeo) {
    fin_parpadeo = ahora + 130;
    prox_parpadeo = ahora + random(2000, 6000);
  }
  if (ahora > prox_mirada) {
    mirada = random(-1, 2);
    prox_mirada = ahora + random(1200, 4000);
  }
}

void ir_a(float x) {
  if (fabsf(cx - x) > 2) {
    objetivo_x = x;
    andando = true;
  }
}

// ============================================================
//  3. DIBUJAR — la interfaz
// ============================================================
float brillo_obj = 140;                     // brillo que pide la pantalla actual

// iconos de 2 px para la barra y la cortina
const char *const ICO_PC_NO[]  = {"XXXXXXX", "X.....X", "X.....X", "XXXXXXX", "..XXX.."};
const char *const ICO_RELOJ[]  = {"..XXX..", ".X...X.", "X..X..X", "X..XX.X", "X.....X", ".X...X.", "..XXX.."};
const char *const ICO_LUNA[]   = {"..XXX..", ".XX....", "XX.....", "XX.....", "XX.....", ".XX....", "..XXX.."};
const char *const ICO_SOL[]    = {"X.X.X", ".XXX.", "XXXXX", ".XXX.", "X.X.X"};
const char *const ICO_CERRAR[] = {"X...X", ".X.X.", "..X..", ".X.X.", "X...X"};
const char *const ICO_AJUSTES[] = {"..X.X..", ".XXXXX.", "XX...XX", ".X...X.", "XX...XX", ".XXXXX.", "..X.X.."};
const char *const ICO_DND[]     = {"..XXX..", ".XXXXX.", "XXXXXXX", "X.....X", "XXXXXXX", ".XXXXX.", "..XXX.."};
const char *const ICO_ZZ[]      = {"XXXX...", "..X....", ".X.XXX.", "XXXX.X.", "....X..", "...XXX."};
const char *const ICO_ONDA[]    = {"....X....", "..X.X.X..", "X.X.X.X.X", "..X.X.X..", "....X...."};
const char *const ICO_HABLA[]   = {"XXXXXXX", "X.....X", "X.X.X.X", "X.....X", "XXXXXXX", ".X.....", "X......"};
const char *const ICO_CAMPANA[] = {"...X...", "..XXX..", ".XXXXX.", ".XXXXX.", "XXXXXXX", "...X..."};
const char *const ICO_TERM[]   = {"X.......", ".X......", "..X.....", ".X......", "X...XXXX"};
const char *const ICO_GALAX[]  = {"...X.....", "..XXX....", ".XXXXX..X", "..XXX..XXX", "...X....X."};   // estrellas
const char *const ICO_MANDO[]  = {".XXXXXXXXX.", "XX.XXXXX.XX", "X...XXX.X.X", "XX.XXXXX.XX", ".XXX...XXX."};

// Mezcla dos colores (t = 0 -> a, t = 1 -> b)
uint16_t mezclar(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bg - ag) * t) << 5) | (int)(ab + (bb - ab) * t);
}

// micro tachado: el icono, y una raya con un hueco para que se lea bien
void icono_micro_no(int x, int y, uint16_t col, uint16_t fondo) {
  patron(g, ICO_MICRO, 8, x, y, 2, col);
  patron(g, ICO_TACHON, 8, x + 2, y, 2, fondo);
  patron(g, ICO_TACHON, 8, x, y, 2, col);
}

// Batería: neutra; verde si se carga, roja si queda poca
void dibujar_bateria(int x, int y) {
  g.drawRect(x, y, 20, 10, TXT2);
  g.fillRect(x + 20, y + 3, 2, 4, TXT2);
  uint16_t col = pc_carga ? VERDE : (pc_bat <= 20 ? ROJO : TXT2);
  g.fillRect(x + 2, y + 2, max(2, 16 * pc_bat / 100), 6, col);
}

// La barra de estado, como la de un móvil: la hora, lo que hace Claude y,
// a la derecha, solo los iconos que avisan de algo
void dibujar_barra_arriba() {
  g.fillRect(0, 0, 240, BARRA_H, FONDO);
  long s = hora_seg();
  char hora[8];
  if (s >= 0) snprintf(hora, sizeof(hora), "%02ld:%02ld", s / 3600, (s / 60) % 60);
  else strcpy(hora, "--:--");
  g.setFont(F_TXT);
  g.setTextDatum(middle_left);
  g.setTextColor(s >= 0 ? TXT : TXT3);
  g.drawString(hora, 10, 12);
  int x = 10 + g.textWidth(hora) + 10;
  if (claude_prev == "trabajando") dibujar_spinner(g, x + 4, 12, ahora, CLAWD, 2);
  else if (claude_prev == "esperando") g.fillRect(x, 9, 6, 6, AMARILLO);
  else if (alerta == AL_LISTO) g.fillRect(x, 9, 6, 6, VERDE);

  int xr = 230;                              // de derecha a izquierda
  g.setFont(F_PEQ);
  g.setTextDatum(middle_right);
  g.setTextColor(TXT2);
  if (pc_bat >= 0) {
    dibujar_bateria(xr - 22, 7);
    String pct = String(pc_bat) + "%";
    g.drawString(pct, xr - 26, 12);
    xr -= 26 + g.textWidth(pct) + 10;
  }
  if (pomo != POMO_NO || temp == T_CORRIENDO || temp == T_PAUSA) {
    uint32_t ms = temp == T_CORRIENDO ? (temp_fin > ahora ? temp_fin - ahora : 0)
                : temp == T_PAUSA ? temp_queda : (pomo_fin > ahora ? pomo_fin - ahora : 0);
    String m = ms < 60000 ? String(ms / 1000 + 1) + " s" : String(ms / 60000 + 1) + "'";
    g.drawString(m, xr, 12);
    xr -= g.textWidth(m) + 4;
    patron(g, ICO_RELOJ, 7, xr - 14, 5, 2, TXT2);
    xr -= 24;
  }
  if (!pc_conectado()) { patron(g, ICO_PC_NO, 5, xr - 14, 7, 2, TXT3); g.drawLine(xr - 16, 18, xr - 1, 5, TXT2); xr -= 24; }
  if (audio_mudo) { patron(g, ICO_ALTAVOZ_NO, 7, xr - 18, 5, 2, TXT2); xr -= 26; }
  if (mic_mudo)   { icono_micro_no(xr - 14, 4, TXT2, FONDO); xr -= 22; }
  if (no_molestar) { patron(g, ICO_DND, 7, xr - 14, 5, 2, TXT2); xr -= 22; }
  if (vad_activo || voz == VOZ_ESCUCHA) { g.fillRect(xr - 6, 9, 6, 6, VERDE); xr -= 14; }   // el micro está mandando audio
}

void dibujar_suelo() {
  for (int x = 5; x < 55; x += 3) g.fillRect(x * L, SUELO + 2 * L, L, L, SUELO_COL);
}

// barra de progreso: pista un escalón más clara que la tarjeta
void barra(int x, int y, int w, int h, float frac, uint16_t col) {
  g.fillRect(x, y, w, h, SUP2);
  int fw = constrain((int)(w * frac), 0, w);
  if (fw > 0) g.fillRect(x, y, fw, h, col);
}

// tarjeta: superficie + borde de 1 px
void tarjeta(int x, int y, int w, int h, uint16_t fondo = SUP, uint16_t borde = BORDE) {
  g.fillRoundRect(x, y, w, h, 8, fondo);
  g.drawRoundRect(x, y, w, h, 8, borde);
}

// texto centrado en el panel, en una o dos líneas (parte por el espacio más centrado)
void texto_panel(const String &txt, uint16_t col, const String &sub = "") {
  g.setFont(F_TXT);
  g.setTextColor(col);
  g.setTextDatum(middle_center);
  if (sub.length()) {
    g.drawString(txt, 120, PANEL_Y + 21);
    g.setFont(F_PEQ);
    g.setTextColor(TXT2);
    g.drawString(sub, 120, PANEL_Y + 41);
    return;
  }
  if (g.textWidth(txt) <= 200) {
    g.drawString(txt, 120, PANEL_Y + 31);
    return;
  }
  int mitad = txt.length() / 2, corte = -1;
  for (int d = 0; d < mitad && corte < 0; d++) {
    if (txt[mitad + d] == ' ') corte = mitad + d;
    else if (txt[mitad - d] == ' ') corte = mitad - d;
  }
  if (corte < 0) corte = byte_de_letra(txt, letras(txt) / 2);   // sin espacios (chino): por la mitad
  g.drawString(txt.substring(0, corte), 120, PANEL_Y + 22);
  g.drawString(txt.substring(txt[corte] == ' ' ? corte + 1 : corte), 120, PANEL_Y + 40);
}

void fila_dato(const char *nombre, const String &valor, float frac, uint16_t col) {
  g.setFont(F_TXT);
  g.setTextColor(TXT);
  g.setTextDatum(middle_left);
  g.drawString(nombre, 22, PANEL_Y + 22);
  g.setFont(F_PEQ);
  g.setTextDatum(middle_right);
  g.setTextColor(TXT2);
  g.drawString(valor, 218, PANEL_Y + 22);
  barra(22, PANEL_Y + 38, 196, 6, frac, col);
}

String mmss(uint32_t ms) {
  uint32_t s = ms / 1000;
  char b[12];
  snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  return b;
}

void dibujar_panel() {
  tarjeta(8, PANEL_Y + 2, 224, 58);

  if (voz == VOZ_ESCUCHA) {
    fila_dato(T("Te escucho"), T("suelta para enviar"), min(1.0f, nivel_voz / 1500), TXT);
    return;
  }
  if (voz == VOZ_TRANSCRIBE) {
    texto_panel(T("Entendiendo lo que dices…"), TXT2);
    return;
  }
  if (ahora < aviso_hasta) {
    texto_panel(aviso_txt, TXT);
    return;
  }
  if (claude_prev == "esperando") {
    texto_panel(T("Claude te necesita"), AMARILLO, T("Pide permiso en la terminal"));
    return;
  }
  if (claude_prev == "trabajando") {
    dibujar_spinner(g, 30, PANEL_Y + 31, ahora, CLAWD, 3);
    uint32_t s = (ahora - claude_desde) / 1000;
    const char *verbo = VERBOS[((ahora - claude_desde) / 4000 + claude_desde) % N_VERBOS];
    g.setFont(F_TXT);
    g.setTextDatum(middle_left);
    g.setTextColor(CLAWD);
    g.drawString(String(verbo) + "…", 50, PANEL_Y + 22);
    g.setFont(F_PEQ);
    g.setTextColor(TXT2);
    String t = s < 60 ? String(s) + " s" : String(s / 60) + " min " + String(s % 60) + " s";
    g.drawString("Claude Code · " + t, 50, PANEL_Y + 41);
    return;
  }
  if (pomo != POMO_NO) {
    bool foco = pomo == POMO_FOCO;
    uint32_t total = foco ? FOCO_MS : DESCANSO_MS;
    uint32_t queda = pomo_fin > ahora ? pomo_fin - ahora : 0;
    fila_dato(foco ? T("Foco") : T("Descanso"), mmss(queda), 1 - (float)queda / total, foco ? VERDE : TXT2);
    return;
  }
  if (recordatorio >= 0) {
    texto_panel(T("Toca para decir que sí"), TXT2);
    return;
  }

  if (ahora > prox_pagina) {
    pagina = (pagina + 1) % 3;
    if (pagina == 2 && pc_bat < 0) pagina = 0;   // sin batería, solo CPU y RAM
    prox_pagina = ahora + 7000;
  }
  if (!pc_conectado()) {
    texto_panel(T("Sin conexión con el PC"), TXT2, T("El companion no está en marcha"));
    return;
  }
  // el color solo cuando hay algo que contar (caliente, llena, poca batería)
  switch (pagina) {
    case 0: {
      uint16_t col = pc_temp >= 85 ? ROJO : (pc_temp >= 75 ? AMARILLO : TXT2);
      fila_dato("CPU", String((int)pc_cpu) + " %  ·  " + String((int)pc_temp) + " °C", pc_cpu / 100, col);
      break;
    }
    case 1:
      fila_dato(T("Memoria"), String((int)pc_ram) + " %", pc_ram / 100, pc_ram > 90 ? ROJO : TXT2);
      break;
    case 2:
      if (pc_bat >= 0)
        fila_dato(T("Batería"), String(pc_bat) + " %" + (pc_carga ? T("  ·  cargando") : ""), pc_bat / 100.0f,
                  pc_carga ? VERDE : (pc_bat <= 20 ? ROJO : TXT2));
      break;
  }
}

// cartel que Clawd sujeta con los brazos (recordatorios)
void dibujar_cartel(int cabeza_y, const char *txt) {
  int c = rejilla(cx), arriba = rejilla(cabeza_y);
  int w = max(30, ancho_pixel(txt) + 4), h = 9;
  int x = constrain(c - w / 2, 1, 59 - w), y = arriba - h - 4;
  // palitos desde el cartel hasta las pinzas (que están en c-16 y c+14)
  g.fillRect((c - 16) * L, (y + h) * L, L, 6 * L, GRIS);
  g.fillRect((c + 15) * L, (y + h) * L, L, 6 * L, GRIS);
  caja_pixel(g, x, y, w, h, L, CREMA);
  texto_pixel(g, txt, x + (w - ancho_pixel(txt)) / 2, y + 2, L, OJO);
}

// ============================================================
//  La pantalla de la terminal
// ============================================================
const uint16_t TERM_FONDO = FONDO;

// Cabecera de las pantallas de Claude: Clawd pequeño, título y subtítulo
void cabecera_claude(const String &sub, bool trabaja, bool espera, bool escucha) {
  Pose mini;
  mini.p = 1; mini.x = 26; mini.suelo = 54;
  mini.ojos = ahora < fin_parpadeo ? OJOS_CERRADOS : OJOS_NORMAL;
  if (trabaja) {
    mini.piernas = PIERNAS_SENTADO;
    mini.brazo_i = ((ahora / 110) % 3 == 0) ? -1 : 0;
    mini.brazo_d = ((ahora / 130) % 3 == 1) ? -1 : 0;
  } else if (espera) {
    mini.ojos = OJOS_GRANDES;
    mini.brazo_i = (ahora / 250) % 2; mini.brazo_d = !mini.brazo_i;
  } else if (escucha) {
    mini.ojos = OJOS_GRANDES; mini.brazo_d = 1;
  } else if (alerta == AL_LISTO) {
    mini.ojos = OJOS_FELICES;
  }
  dibujar_clawd(g, mini);
  g.setTextDatum(middle_left);
  g.setFont(F_GRANDE);
  g.setTextColor(TXT);
  g.drawString("Claude Code", 50, 38);
  g.setFont(letra_para(sub.c_str(), F_PEQ));
  g.setTextColor(TXT2);
  g.drawString(sub, 50, 54);
  if (trabaja) dibujar_spinner(g, 222, 42, ahora, CLAWD, 2);
  g.drawFastHLine(10, 63, 220, BORDE);
}

void fotograma_terminal() {
  bool trabaja = voz == VOZ_CLAUDE || pc_claude_voz;
  g.fillScreen(TERM_FONDO);
  cabecera_claude(trabaja ? T("toca para parar") : T("por voz · en tu carpeta"), trabaja, false, voz == VOZ_ESCUCHA);

  // historial
  g.setFont(F_TXT);
  g.setTextDatum(top_left);
  int hay = min(term_total, TERM_N);
  int ultima = term_total - 1 - term_scroll;
  for (int f = 0; f < TERM_FILAS; f++) {
    int i = ultima - (TERM_FILAS - 1 - f);
    if (i < 0 || i < term_total - hay) continue;
    const LineaTerm &l = term[i % TERM_N];
    g.setTextColor(l.col);
    texto_mixto(g, l.txt, 8, 70 + f * 16, F_TXT, top_left);
  }
  if (term_total == 0) {
    g.setTextColor(TXT2);
    g.drawString(T("Mantén pulsado y habla."), 10, 82);
    g.setTextColor(TXT3);
    g.drawString(T("Lo que digas se lo paso a"), 10, 112);
    g.drawString(T("Claude Code: puede leer y"), 10, 128);
    g.drawString(T("editar archivos de este"), 10, 144);
    g.drawString(T("ordenador."), 10, 160);
  }
  if (term_scroll > 0) patron(g, ICO_TACHON, 1, 226, 286, 2, TXT3);   // no estás viendo lo último

  // línea de estado
  g.fillRect(0, 298, 240, 22, SUP);
  g.drawFastHLine(0, 298, 240, BORDE);
  g.setFont(F_PEQ);
  g.setTextDatum(middle_left);
  const int ys = 309;
  switch (voz) {
    case VOZ_ESCUCHA:
      g.setTextColor(TXT);
      g.drawString(T("Te escucho…"), 10, ys);
      barra(110, ys - 3, 120, 6, min(1.0f, nivel_voz / 1500), TXT);
      break;
    case VOZ_TRANSCRIBE:
      g.setTextColor(TXT2);
      g.drawString(T("Entendiendo…"), 10, ys);
      break;
    case VOZ_CUENTA: {
      int s = cuenta_fin > ahora ? (cuenta_fin - ahora) / 1000 + 1 : 0;
      g.setTextColor(AMARILLO);
      g.drawString(s > 0 ? T("Envío en ") + String(s) + T(" s · toca para cancelar") : T("Enviando…"), 10, ys);
      break;
    }
    default:
      if (trabaja) {
        dibujar_spinner(g, 14, ys, ahora, CLAWD, 2);
        g.setTextColor(CLAWD);
        g.drawString(String(VERBOS[(ahora / 4000) % N_VERBOS]) + "…", 26, ys);
      } else {
        g.setTextColor(TXT3);
        g.drawString(T("Mantén pulsado para hablar"), 10, ys);
      }
  }

  if (voz == VOZ_ESCUCHA) led(24, 24, 24);
  else if (trabaja) led_pulso(1600, 30, 12, 6);
  else led(3, 1, 0);
  brillo_obj = 140;
}

// ============================================================
//  El fotograma completo
// ============================================================
void fotograma() {
  const bool claude_trabaja = claude_prev == "trabajando";
  const bool claude_espera = claude_prev == "esperando";
  const bool calor = pc_conectado() && pc_temp >= 85;
  const bool cansado = pc_conectado() && pc_bat >= 0 && pc_bat < 20 && !pc_carga;

  // ---- elegir modo por prioridad ----
  enum { M_ESCUCHA, M_ANIM, M_REAC, M_ESPERA, M_CARTEL, M_MIMOS, M_TRABAJA, M_DORMIR, M_BAILE, M_LIBRE } modo;
  Fotograma fa;
  bool hay_anim = anim_fotograma(ahora, fa);
  if (voz == VOZ_ESCUCHA || voz == VOZ_TRANSCRIBE || claude_espera || durmiendo || mimando) {
    anim_actual = -1;                       // esto es más importante: cortar la animación
    hay_anim = false;
  }
  if (voz == VOZ_ESCUCHA || voz == VOZ_TRANSCRIBE) modo = M_ESCUCHA;
  else if (hay_anim)           modo = M_ANIM;
  else if (reac != R_NADA)     modo = M_REAC;
  else if (claude_espera)      modo = M_ESPERA;
  else if (recordatorio >= 0)  modo = M_CARTEL;
  else if (mimando)            modo = M_MIMOS;
  else if (bailando)           modo = M_BAILE;
  else if (claude_trabaja)     modo = M_TRABAJA;
  else if (durmiendo)          modo = M_DORMIR;
  else                         modo = M_LIBRE;

  bool libre = modo == M_LIBRE && pomo != POMO_FOCO;
  if (modo == M_TRABAJA || modo == M_CARTEL) ir_a(120);
  if (modo == M_DORMIR || modo == M_MIMOS) andando = false;
  float vel = 1.3f * (pc_cpu > 70 ? 1.8f : 1) * (cansado ? 0.5f : 1);
  mover_cuerpo(libre, vel);

  // ---- pose base: respirar, parpadear, andar ----
  Pose p;
  p.x = cx;
  p.suelo = SUELO;
  p.alto = alto;
  // fotogramas: aplastado al caer, estirado al subir, y respirar (1 píxel)
  p.forma = ahora < aplastado_hasta ? FORMA_APLASTADO : (alto > 0 && vy < -2 ? FORMA_ESTIRADO : FORMA_NORMAL);
  p.respira = (ahora / 900) % 2;
  p.mirar = andando ? dir : mirada;
  p.ojos = ahora < fin_parpadeo ? OJOS_CERRADOS : (cansado ? OJOS_CANSADOS : OJOS_NORMAL);
  p.piernas = andando ? ((ahora / 140) % 2 ? PIERNAS_A : PIERNAS_B) : PIERNAS_QUIETAS;
  p.cinta = pomo == POMO_FOCO;
  if (calor) p.color = CLAWD_CALOR;

  const char *texto_bocadillo = nullptr;
  bool parpadeo = ahora < fin_parpadeo;
  uint32_t t_reac = ahora - reac_desde;

  switch (modo) {
    case M_ANIM:
      andando = false;
      aplicar_fotograma(fa, p, (int)cx - 64);
      if (fa.offset <= 0) {                 // si está visible, el bocadillo de la reacción
        if (reac == R_LISTO) texto_bocadillo = T("Listo!");
        else if (reac == R_POMO_FIN) texto_bocadillo = T("Descanso!");
      }
      break;

    case M_ESCUCHA:
      p.ojos = parpadeo ? OJOS_CERRADOS : OJOS_GRANDES;
      p.mirar = 0;
      p.brazo_d = 1;                       // la pinza en la "oreja"
      texto_bocadillo = voz == VOZ_ESCUCHA ? T("Te escucho...") : "Mmm...";
      andando = false;
      break;

    case M_REAC:
      switch (reac) {
        case R_HOLA:
          p.brazo_d = (t_reac / 220) % 2;
          texto_bocadillo = T("Hola!");
          break;
        case R_SALTO:
        case R_CONTENTO:
          p.ojos = OJOS_FELICES;
          if (reac == R_SALTO) p.brazo_i = p.brazo_d = 1;
          break;
        case R_SUSTO:
          p.ojos = OJOS_GRANDES;
          p.mirar = 0;
          p.brazo_i = p.brazo_d = 1;
          texto_bocadillo = "!";
          break;
        case R_LISTO:
        case R_POMO_FIN:
        case R_DESCANSO_FIN:
          p.ojos = OJOS_FELICES;
          p.brazo_i = (t_reac / 180) % 2;
          p.brazo_d = 1 - p.brazo_i;
          texto_bocadillo = reac == R_LISTO ? T("Listo!") : (reac == R_POMO_FIN ? T("Descanso!") : "A por otra!");
          if (t_reac < 1200 && (t_reac / 150) % 2 == 0) soltar_confeti(cx, SUELO - 80, 1);
          if (t_reac > 900 && t_reac < 1000) saltar(7);
          break;
        case R_DESPERTAR:
          p.ojos = t_reac < 900 ? OJOS_CANSADOS : OJOS_NORMAL;
          p.mirar = 0;
          texto_bocadillo = "Eh? Ya voy...";
          break;
        default: break;
      }
      break;

    case M_ESPERA:
      p.ojos = parpadeo ? OJOS_CERRADOS : OJOS_GRANDES;
      p.mirar = 0;
      p.brazo_i = (ahora / 250) % 2;
      p.brazo_d = 1 - p.brazo_i;
      if ((ahora / 600) % 2) texto_bocadillo = "?";
      if (ahora % 1400 < 40) saltar(5);
      break;

    case M_CARTEL:
      p.brazo_i = p.brazo_d = 1;
      p.mirar = 0;
      break;

    case M_MIMOS:
      p.ojos = OJOS_FELICES;
      p.mirar = 0;
      p.forma = (ahora / 300) % 2 ? FORMA_APLASTADO : FORMA_NORMAL;   // se achucha
      break;

    case M_TRABAJA:
      if (!andando) {
        p.piernas = PIERNAS_SENTADO;
        p.mirar = 0;
        // teclear: los brazos suben y bajan a ritmos distintos
        p.brazo_i = ((ahora / 110) % 3 == 0) ? -1 : 0;
        p.brazo_d = ((ahora / 130) % 3 == 1) ? -1 : 0;
      }
      break;

    case M_DORMIR:
      p.ojos = OJOS_CERRADOS;
      p.mirar = 0;
      p.piernas = PIERNAS_SENTADO;
      p.forma = FORMA_APLASTADO;
      p.respira = (ahora / 1400) % 2;
      p.color = CLAWD;
      {
        static uint32_t prox_z = 0;
        if (ahora > prox_z) {
          soltar(P_ZETA, cx + 40, SUELO - 60, 0.4f, -0.7f, 70, GRIS);
          prox_z = ahora + 1300;
        }
      }
      break;

    case M_BAILE:
      p.ojos = parpadeo ? OJOS_CERRADOS : OJOS_FELICES;
      {
        uint32_t paso = ahora / 280;          // un paso de baile cada 280 ms
        p.forma = paso % 2 ? FORMA_ESTIRADO : FORMA_APLASTADO;
        p.x += (paso / 2) % 2 ? 2 * L : -2 * L;
        p.brazo_i = paso % 2;
        p.brazo_d = !p.brazo_i;
      }
      {
        static uint32_t prox_nota = 0;
        if (ahora > prox_nota) {
          soltar(P_NOTA, cx + random(-60, 61), SUELO - 70, 0, -1.1f, 55, random(2) ? AMARILLO : CREMA);
          prox_nota = ahora + 500;
        }
      }
      break;

    case M_LIBRE:
      if (pomo == POMO_FOCO) p.mirar = 0;
      break;
  }

  // hablando: se estira con las sílabas fuertes y gesticula con un brazo
  if (rep_nivel > 150 && modo != M_DORMIR && modo != M_ANIM && modo != M_ESCUCHA) {
    p.forma = rep_nivel > 2500 ? FORMA_ESTIRADO : FORMA_NORMAL;
    p.respira = false;
    p.brazo_d = (ahora / 350) % 2;
    if (p.ojos == OJOS_CERRADOS && ahora >= fin_parpadeo) p.ojos = OJOS_NORMAL;
    andando = false;
  }

  // efectos que se suman al modo
  if (calor && modo != M_DORMIR) {
    static uint32_t prox_gota = 0;
    if (ahora > prox_gota) {
      int lado = random(2) ? 1 : -1;
      soltar(P_GOTA, cx + lado * 50, SUELO - 75, lado * 0.6f, -1.5f, 40, TXT2);
      prox_gota = ahora + 800;
    }
    if (modo == M_LIBRE) p.brazo_d = (ahora / 300) % 2;   // se abanica
  }

  // ---- dibujar ----
  g.fillScreen(FONDO);
  dibujar_suelo();
  int cabeza = dibujar_clawd(g, p);
  if (modo == M_ANIM) dibujar_poof(g, fa, (int)p.x, SUELO);
  if (modo == M_TRABAJA && !andando) dibujar_spinner(g, (int)cx + 52, cabeza - 18, ahora, CLAWD);
  if (modo == M_CARTEL) dibujar_cartel(cabeza, T(RECORDATORIOS[recordatorio]));
  dibujar_parts(g);
  if (texto_bocadillo) bocadillo(g, (int)cx, cabeza - 6, T(texto_bocadillo), CREMA, OJO);
  dibujar_panel();

  // ---- LED: siempre fundidos, nada de parpadeos ----
  if (modo == M_ESCUCHA)                   led(24, 24, 24);
  else if (modo == M_ESPERA)               led_pulso(900, 45, 30, 0);
  else if (modo == M_REAC && reac == R_LISTO) led_pulso(700, 0, 40, 6);
  else if (modo == M_BAILE)                led_pulso(560, 40, 18, 8);
  else if (modo == M_TRABAJA)              led_pulso(1600, 30, 12, 6);
  else if (modo == M_DORMIR)               led_pulso(4000, 3, 1, 0);
  else if (calor)                          led(30, 2, 0);
  else if (pomo == POMO_FOCO)              led(0, 6, 1);
  else                                     led(3, 1, 0);

  brillo_obj = durmiendo ? 18 : 140;
}

// ============================================================
//  La pantalla de la galaxia
// ============================================================
// Número "aleatorio" fijo para cada estrella: siempre sale el mismo,
// así las estrellas no saltan de sitio entre fotogramas.
static uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
  return x;
}
static float azar(uint32_t semilla) { return (hash32(semilla) & 0xFFFF) / 65535.0f; }

uint16_t gris(float v) {                  // 0..1 -> de negro a blanco
  int c = constrain((int)(v * 255), 0, 255);
  return C565(c, c, c);
}

void fotograma_galaxia() {
  const int CX = 120, CY = 172;           // centro de la galaxia
  const float INCL = 0.62f;               // la vemos un poco tumbada (elipses)
  const float R0 = 24, R1 = 112;          // radio del primer y del último anillo
  float t = ahora / 1000.0f;

  g.fillScreen(C565(6, 6, 6));

  // estrellas de fondo que titilan
  for (int i = 0; i < 70; i++) {
    int x = azar(i * 3 + 1) * 240;
    int y = ARRIBA + azar(i * 3 + 2) * (285 - ARRIBA);
    float v = 0.15f + 0.25f * (0.5f + 0.5f * sinf(t * (0.8f + azar(i * 3 + 3) * 2) + i));
    g.drawPixel(x, y, gris(v));
  }

  int n = gal_n > 0 ? gal_n : 6;          // sin datos del PC: una galaxia de muestra
  // anillos (uno por carpeta)
  for (int a = 0; a < n; a++) {
    float r = R0 + (a + 0.5f) * (R1 - R0) / n;
    g.drawEllipse(CX, CY, (int)r, (int)(r * INCL), C565(30, 30, 30));
  }

  // centro que late, cálido como el de Nebula
  float latido = 0.5f + 0.5f * sinf(t * 2.0f);
  g.fillCircle(CX, CY, 13 + (int)(latido * 2), C565(40, 28, 20));
  g.fillCircle(CX, CY, 9, C565(110, 70, 45));
  g.fillCircle(CX, CY, 5, CLAWD);
  g.fillCircle(CX, CY, 2, CREMA);

  // estrellas = notas
  for (int a = 0; a < n; a++) {
    int notas = gal_n > 0 ? gal_notas[a] : 8 + a * 4;
    int m = constrain(notas, 1, 48);      // como mucho 48 por anillo, si no es un borrón
    float r = R0 + (a + 0.5f) * (R1 - R0) / n;
    float vel = 1.6f / sqrtf(r);          // más cerca del centro = gira más rápido
    for (int k = 0; k < m; k++) {
      uint32_t id = a * 131 + k;
      float ang = k * 6.2832f / m + azar(id * 7) * 0.5f + t * vel;
      float rr = r + (azar(id * 7 + 1) - 0.5f) * 6;
      int x = CX + cosf(ang) * rr;
      int y = CY + sinf(ang) * rr * INCL;
      float delante = 0.5f + 0.5f * sinf(ang);   // las de abajo están "más cerca"
      float brillo = 0.45f + 0.4f * delante + 0.15f * sinf(t * 3 + id);
      g.drawPixel(x, y, gris(brillo));
      if (delante > 0.6f) {               // las cercanas, un poco más grandes
        g.drawPixel(x + 1, y, gris(brillo * 0.6f));
        g.drawPixel(x, y + 1, gris(brillo * 0.6f));
      }
    }
  }

  // un cometa de vez en cuando (cada 7 s cruza durante 1,5 s)
  float fase = fmodf(t, 7.0f);
  if (fase < 1.5f) {
    float y0 = ARRIBA + 10 + azar((uint32_t)(t / 7.0f)) * 120;
    float x = -20 + fase / 1.5f * 280, y = y0 + fase / 1.5f * 60;
    for (int c = 0; c < 10; c++)          // la cola
      g.drawPixel(x - c * 3, y - c * 0.65f, gris(0.9f - c * 0.09f));
    g.fillCircle(x, y, 1, CREMA);
  }

  // textos
  g.setTextDatum(middle_center);
  g.setFont(F_TXT);
  g.setTextColor(TXT);
  g.drawString(T("Tu vault"), 120, 44);
  g.setFont(F_PEQ);
  g.setTextColor(TXT3);
  char txt[48];
  if (gal_n > 0) snprintf(txt, sizeof(txt), "%d notas · %d carpetas", gal_total, gal_n);
  else strcpy(txt, "Esperando al PC…");
  g.drawString(txt, 120, 62);
  led(2, 1, 0);
  brillo_obj = 140;
}

// ============================================================
//  El progreso de Claude Code y el aviso grande
// ============================================================
// parte un texto en líneas de 'cols' columnas sin cortar palabras (el chino ocupa 2
// columnas y no tiene espacios: se corta donde llegue, sin partir ningún carácter)
int envolver(const String &t, int cols, String *out, int max_lineas) {
  int n = 0, i = 0, largo = t.length();
  while (i < largo && n < max_lineas) {
    while (i < largo && (t[i] == ' ' || t[i] == '\n')) i++;
    int j = i, c = 0, ultimo_espacio = -1;
    while (j < largo && t[j] != '\n') {
      uint8_t b = t[j];
      int bytes = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
      int ancho = b >= 0xE3 ? 2 : 1;
      if (c + ancho > cols) break;
      if (b == ' ') ultimo_espacio = j;
      c += ancho;
      j += bytes;
    }
    int fin = j;
    if (j < largo && t[j] != '\n' && ultimo_espacio > i) fin = ultimo_espacio;   // no partir palabras
    out[n++] = t.substring(i, fin);
    i = fin;
  }
  if (i < largo && n > 0) out[n - 1] += "…";                       // no cabe todo
  return n;
}

String duracion(uint32_t ms) {
  uint32_t m = ms / 60000;
  if (m < 1) return String(ms / 1000) + " s";
  if (m < 60) return String(m) + " min";
  return String(m / 60) + " h " + String(m % 60) + " min";
}

const char *const ICO_HECHA[]    = {"....X", "...X.", "X.X..", ".X..."};

// recorta un texto para que quepa en 'ancho' píxeles (con la letra actual)
String recortar(const String &t, int ancho) {
  if (g.textWidth(t) <= ancho) return t;
  String r = t;
  while (r.length() && g.textWidth(r + "…") > ancho) {
    int k = letras(r) - 1;
    r = r.substring(0, byte_de_letra(r, k));
  }
  return r + "…";
}

void fotograma_claude() {
  const bool trabaja = claude_prev == "trabajando", espera = claude_prev == "esperando";
  g.fillScreen(TERM_FONDO);
  cabecera_claude(mon_proyecto.length() ? mon_proyecto : String(T("sin sesión")), trabaja, espera, false);

  // estado
  g.setTextDatum(middle_left);
  g.setFont(F_TXT);
  if (trabaja) {
    dibujar_spinner(g, 16, 80, ahora, CLAWD, 2);
    g.setTextColor(CLAWD);
    g.drawString(String(VERBOS[((ahora - claude_desde) / 4000 + claude_desde) % N_VERBOS]) + "…", 30, 80);
    g.setFont(F_PEQ);
    g.setTextColor(TXT2);
    g.setTextDatum(middle_right);
    g.drawString(duracion(ahora - claude_desde), 230, 80);
    g.setTextDatum(middle_left);
  } else if (espera) {
    g.fillRect(12, 77, 6, 6, AMARILLO);
    g.setTextColor(AMARILLO);
    g.drawString(T("Te necesita: pide permiso"), 26, 80);
  } else if (claude_fin) {
    g.fillRect(12, 77, 6, 6, VERDE);
    g.setTextColor(TXT);
    g.drawString(T("Terminado hace ") + duracion(ahora - claude_fin), 26, 80);
  } else {
    g.setTextColor(TXT3);
    g.drawString(T("Ahora no está trabajando"), 10, 80);
  }
  g.setFont(F_PEQ);
  g.setTextColor(TXT3);
  if (pc_tapa) g.drawString(pc_insomne ? T("Tapa cerrada · no se suspende") : T("Tapa cerrada"), 10, 98);
  else if (claude_fin && !trabaja && !espera) g.drawString(T("Ha tardado ") + duracion(claude_duro), 26, 98);

  // tareas
  int hechas = 0, primera = -1;
  for (int i = 0; i < mon_n; i++) {
    if (mon_tareas[i].estado == 2) hechas++;
    else if (primera < 0) primera = i;
  }
  if (mon_n > 0) {
    barra(10, 112, 180, 4, (float)hechas / mon_n, hechas == mon_n ? VERDE : TXT2);
    g.setTextColor(TXT2);
    g.setTextDatum(middle_right);
    g.drawString(String(hechas) + "/" + String(mon_n), 230, 114);
    g.setTextDatum(middle_left);
    const int FILAS = 6;
    int ini = constrain(primera < 0 ? mon_n - FILAS : primera - 1, 0, max(0, mon_n - FILAS));
    g.setFont(F_TXT);
    for (int f = 0; f < FILAS && ini + f < mon_n; f++) {
      const Tarea &t = mon_tareas[ini + f];
      int y = 132 + f * 16;
      if (t.estado == 2)      patron(g, ICO_HECHA, 4, 10, y - 4, 2, VERDE);
      else if (t.estado == 1) dibujar_spinner(g, 15, y, ahora, CLAWD, 1);
      else                    g.drawRect(10, y - 4, 8, 8, TXT3);
      g.setTextColor(t.estado == 2 ? TXT3 : (t.estado == 1 ? TXT : TXT2));
      g.setFont(letra_para(t.txt, F_TXT));
      g.drawString(recortar(t.txt, 202), 26, y);
    }
  } else {
    g.setFont(F_PEQ);
    g.setTextColor(TXT3);
    g.drawString(T("Sin lista de tareas"), 10, 128);
  }

  // últimos pasos
  g.drawFastHLine(10, 230, 220, BORDE);
  g.setFont(F_PEQ);
  g.setTextColor(TXT3);
  g.drawString(T("ÚLTIMOS PASOS"), 10, 242);
  g.setFont(F_PEQ);
  for (int i = 0; i < MON_ACTS; i++) {
    g.setTextColor(i == MON_ACTS - 1 ? TXT : TXT2);
    g.setFont(letra_para(mon_acts[i].c_str(), F_PEQ));
    g.drawString(recortar(mon_acts[i], 216), 10, 258 + i * 15);
  }
  brillo_obj = 140;
}

// oscurece todo el lienzo a la mitad (lo de detrás de un aviso)
void oscurecer() {
  uint16_t *p = (uint16_t *)g.getBuffer();
  if (!p) return;
  for (int i = 0; i < 240 * 320; i++) {
    uint16_t c = __builtin_bswap16(p[i]);    // el lienzo guarda los colores con los bytes al revés
    p[i] = __builtin_bswap16((c >> 1) & 0x7BEF);
  }
}

void dibujar_alerta() {
  if (alerta == AL_NO) return;
  oscurecer();
  const bool listo = alerta == AL_LISTO, tiempo = alerta == AL_TIEMPO;
  const uint16_t col = listo ? VERDE : AMARILLO;
  // el borde "respira" entre el gris y el color del estado (sin parpadeos bruscos)
  float k = 0.5f + 0.5f * sinf(ahora / 380.0f);
  g.fillRoundRect(14, 56, 212, 232, 12, SUP);
  g.drawRoundRect(14, 56, 212, 232, 12, mezclar(BORDE, col, k));
  g.drawRoundRect(15, 57, 210, 230, 11, mezclar(SUP, col, k * 0.5f));
  texto_centrado(g, listo ? T("LISTO") : (tiempo ? T("TIEMPO") : T("TE NECESITO")), 72, L, col);
  Pose p;
  p.p = 2; p.x = 120; p.suelo = 150; p.sombra = 0;
  p.ojos = listo ? OJOS_FELICES : OJOS_GRANDES;
  p.brazo_i = (ahora / 260) % 2; p.brazo_d = !p.brazo_i;
  if (listo && (ahora / 520) % 2) p.alto = 4;
  dibujar_clawd(g, p);
  g.setTextDatum(top_center);
  String titulo_aviso = tiempo ? String(T("Temporizador")) : (mon_proyecto.length() ? mon_proyecto : String("Claude Code"));
  g.setFont(letra_para(titulo_aviso.c_str(), F_TXT));
  g.setTextColor(TXT);
  g.drawString(recortar(titulo_aviso, 196), 120, 158);
  g.setFont(F_PEQ);
  if (tiempo) {
    g.setTextColor(TXT2);
    g.drawString(T("Han pasado ") + duracion(temp_dur * 1000UL) + ".", 120, 184);
  } else if (listo) {
    g.setTextColor(TXT3);
    g.drawString(T("ha tardado ") + duracion(claude_duro), 120, 176);
    String lineas[4];
    int n = envolver(mon_resumen, 30, lineas, 4);
    g.setTextColor(TXT2);
    for (int i = 0; i < n; i++) texto_mixto(g, lineas[i], 120, 198 + i * 15, F_PEQ, top_center);
  } else {
    g.setTextColor(TXT2);
    g.drawString(T("Te pide permiso en la terminal."), 120, 184);
    g.setTextColor(TXT3);
    g.drawString(pc_tapa ? T("Abre el portátil") : T("Mira la pantalla del PC"), 120, 202);
  }
  g.setTextColor(TXT3);
  g.drawString(T("Toca para cerrar"), 120, 266);
  g.setTextDatum(top_left);
}

// ============================================================
//  El temporizador
// ============================================================
#define SONAR_SIEMPRE(arr) sonar_siempre(arr, sizeof(arr) / sizeof(arr[0]))
const Nota S_ALARMA[] = {{1760, 1760, 90, 45, 0}, {0, 0, 60, 0, 0}, {1760, 1760, 90, 45, 0}, {0, 0, 60, 0, 0},
                         {1760, 1760, 90, 45, 0}};

void sonar_siempre(const Nota *n, int k) {        // la alarma suena aunque esté en silencio
  bool m = audio_mudo, d = no_molestar;
  audio_mudo = no_molestar = false;
  sonar(n, k);
  audio_mudo = m;
  no_molestar = d;
}

void temp_empezar(uint32_t segundos) {
  temp_dur = segundos;
  temp_fin = ahora + segundos * 1000UL;
  temp = T_CORRIENDO;
  despertar(false);
  SONAR(S_POMO_INI);
  aviso(T("Temporizador: ") + duracion(segundos * 1000UL));
}

void temp_cancelar() {
  if (temp == T_SONANDO && alerta == AL_TIEMPO) alerta = AL_NO;
  temp = T_PARADO;
}

void temp_actualizar() {
  if (temp == T_CORRIENDO && ahora >= temp_fin) {
    temp = T_SONANDO;
    temp_suena_desde = ahora;
    temp_prox_pitido = ahora;
    alerta = AL_TIEMPO;
    despertar(false);
    notificar(2, T("Se acabó el tiempo"), T("Temporizador de ") + duracion(temp_dur * 1000UL));
  }
  if (temp == T_SONANDO) {
    if (alerta != AL_TIEMPO) { temp = T_PARADO; return; }          // lo has cerrado
    if (ahora >= temp_prox_pitido && ahora - temp_suena_desde < 60000) {   // 1 min sonando
      SONAR_SIEMPRE(S_ALARMA);
      temp_prox_pitido = ahora + 1600;
    }
  }
}

// --- dibujo ---
const int RJ_BOT_Y = 266, RJ_BOT_H = 40;

// ---- la rueda: como el selector de hora de Android ----
// Primero las HORAS (0-11 alrededor de la esfera) y, al soltar, los MINUTOS (0-59).
// El tirador va siempre a donde está el dedo (no "suma vueltas"): así no hay sorpresas.
float angulo_dedo(int x, int y) {                // 0 arriba, crece en el sentido del reloj
  float a = atan2f(y - RUEDA_Y, x - RUEDA_X) * 180 / PI + 90;
  return a < 0 ? a + 360 : a;
}

bool en_rueda(int x, int y) {
  float d = hypotf(x - RUEDA_X, y - RUEDA_Y);
  return d > 44 && d < RUEDA_R + 34;             // el centro queda para tocar horas / minutos
}

void rueda_poner(int x, int y) {
  float a = angulo_dedo(x, y);
  uint32_t h = temp_dur / 3600, m = temp_dur / 60 % 60;
  if (rueda_modo == 0) h = (uint32_t)lroundf(a / 30) % 12;   // 12 posiciones
  else                 m = (uint32_t)lroundf(a / 6) % 60;    // 60 posiciones
  uint32_t nuevo = h * 3600 + m * 60;
  if (nuevo != temp_dur) {
    temp_dur = nuevo;
    SONAR(S_TIC);                                // el "clic" de cada muesca
  }
}

void rueda_empezar(int x, int y) { rueda_arrastre = true; rueda_poner(x, y); }
void rueda_mover(int x, int y) { rueda_poner(x, y); }
void rueda_soltar() {
  rueda_arrastre = false;
  if (rueda_modo == 0) { rueda_modo = 1; SONAR(S_CLIC); }   // horas listas: a por los minutos
}

void boton(int x, int y, int w, int h, const char *txt, bool principal) {
  if (principal) {                           // el principal: claro sobre oscuro (lo que más destaca)
    g.fillRoundRect(x, y, w, h, 10, TXT);
    g.setTextColor(FONDO);
  } else {
    g.fillRoundRect(x, y, w, h, 10, SUP);
    g.drawRoundRect(x, y, w, h, 10, BORDE);
    g.setTextColor(TXT);
  }
  g.setFont(F_TXT);
  g.setTextDatum(middle_center);
  g.drawString(txt, x + w / 2, y + h / 2);
}

// punto de la circunferencia a 'grados' desde arriba
void punto_rueda(float grados, float r, int &x, int &y) {
  float a = (grados - 90) * PI / 180;
  x = RUEDA_X + (int)lroundf(cosf(a) * r);
  y = RUEDA_Y + (int)lroundf(sinf(a) * r);
}

void dibujar_reloj() {
  if (!reloj_abierto) return;
  g.fillRect(0, BARRA_H, 240, 320 - BARRA_H, FONDO);
  g.setFont(F_GRANDE);
  g.setTextDatum(middle_left);
  g.setTextColor(TXT);
  g.drawString(T("Temporizador"), 14, 44);
  patron(g, ICO_CERRAR, 5, 212, 39, 2, TXT2);   // la X para cerrar

  const bool corre = temp == T_CORRIENDO || temp == T_PAUSA;
  const int r0 = RUEDA_R - 12, r1 = RUEDA_R;
  g.fillArc(RUEDA_X, RUEDA_Y, r0, r1, 0, 360, SUP2);

  if (corre) {
    // corriendo: el anillo es lo que queda del total, y el tiempo en el centro
    uint32_t ms = temp == T_CORRIENDO ? (temp_fin > ahora ? temp_fin - ahora : 0) : temp_queda;
    float frac = temp_dur ? (float)ms / (temp_dur * 1000UL) : 0;
    uint16_t col = temp == T_PAUSA ? AMARILLO : VERDE;
    if (frac > 0.002f) g.fillArc(RUEDA_X, RUEDA_Y, r0, r1, 270, 270 + 360 * frac, col);
    uint32_t sg = (ms + 999) / 1000;
    char t[12];
    if (sg >= 3600) snprintf(t, sizeof(t), "%lu:%02lu:%02lu", (unsigned long)(sg / 3600), (unsigned long)(sg / 60 % 60), (unsigned long)(sg % 60));
    else snprintf(t, sizeof(t), "%02lu:%02lu", (unsigned long)(sg / 60), (unsigned long)(sg % 60));
    uint16_t ct = temp == T_PAUSA ? mezclar(TXT3, TXT, 0.5f + 0.5f * sinf(ahora / 300.0f)) : TXT;
    texto_centrado(g, t, RUEDA_Y - 12, sg >= 3600 ? 3 : 4, ct);
    g.setFont(F_PEQ);
    g.setTextDatum(middle_center);
    g.setTextColor(TXT3);
    g.drawString(temp == T_PAUSA ? T("en pausa") : T("quedan"), RUEDA_X, RUEDA_Y + 22);
  } else {
    // eligiendo: la esfera con sus números (horas 0-11 o minutos de 5 en 5)
    uint32_t h = temp_dur / 3600, m = temp_dur / 60 % 60;
    int valor = rueda_modo == 0 ? h : m;
    float ang = rueda_modo == 0 ? h * 30.0f : m * 6.0f;
    if (ang > 0.5f) g.fillArc(RUEDA_X, RUEDA_Y, r0, r1, 270, 270 + ang, TXT2);
    g.setFont(F_PEQ);
    g.setTextDatum(middle_center);
    for (int i = 0; i < 12; i++) {
      int n = rueda_modo == 0 ? i : i * 5;
      int x, y;
      punto_rueda(i * 30, RUEDA_R + 16, x, y);
      g.setTextColor(n == valor ? TXT : TXT3);
      g.drawString(String(n), x, y);
    }
    int kx, ky;                                   // el tirador, donde está el valor
    punto_rueda(ang, (r0 + r1) / 2.0f, kx, ky);
    g.fillCircle(kx, ky, rueda_arrastre ? 12 : 10, TXT);
    g.fillCircle(kx, ky, 4, FONDO);
    // centro: H:MM, la parte que eliges en blanco y la otra apagada
    char sh[4], sm[4];
    snprintf(sh, sizeof(sh), "%lu", (unsigned long)h);
    snprintf(sm, sizeof(sm), "%02lu", (unsigned long)m);
    const int p = 4;
    int wh = ancho_pixel(sh), wm = ancho_pixel(sm);
    int total = wh + 3 + wm;                        // "H" + " : " + "MM" (en casillas de 4 px)
    int x0 = (240 / p - total) / 2, y0 = (RUEDA_Y - 12) / p;
    texto_pixel(g, sh, x0, y0, p, rueda_modo == 0 ? TXT : TXT3);
    texto_pixel(g, ":", x0 + wh + 1, y0, p, TXT3);
    texto_pixel(g, sm, x0 + wh + 3, y0, p, rueda_modo == 1 ? TXT : TXT3);
    g.setFont(F_PEQ);
    g.setTextDatum(middle_center);
    g.setTextColor(TXT3);
    g.drawString(rueda_modo == 0 ? T("horas") : T("minutos"), RUEDA_X, RUEDA_Y + 22);
  }

  // botones
  if (temp == T_PARADO) {
    boton(10, RJ_BOT_Y, 106, RJ_BOT_H, pomo == POMO_NO ? T("Pomodoro") : T("Sin pomodoro"), false);
    boton(124, RJ_BOT_Y, 106, RJ_BOT_H, T("Empezar"), temp_dur > 0);
  } else {
    boton(10, RJ_BOT_Y, 106, RJ_BOT_H, T("Cancelar"), false);
    boton(124, RJ_BOT_Y, 106, RJ_BOT_H, temp == T_PAUSA ? T("Seguir") : T("Pausar"), true);
  }
  g.setTextDatum(top_left);
}

void reloj_toque(int x, int y) {
  SONAR(S_CLIC);
  interaccion();
  if (y < 64 && x > 190) { reloj_abierto = false; return; }        // la X
  if (temp == T_PARADO && hypotf(x - RUEDA_X, y - RUEDA_Y) <= 44) {  // el centro: horas o minutos
    rueda_modo = x < RUEDA_X ? 0 : 1;
    return;
  }
  if (y >= RJ_BOT_Y && y < RJ_BOT_Y + RJ_BOT_H) {
    bool izq = x < 120;
    if (temp == T_PARADO) {
      if (izq) {
        if (pomo == POMO_NO) { empezar_pomodoro(); reloj_abierto = false; }
        else { pomo = POMO_NO; aviso(T("Pomodoro cancelado")); }
      } else if (temp_dur > 0) {
        temp_empezar(temp_dur);
      }
    } else if (izq) {
      temp_cancelar();
    } else if (temp == T_CORRIENDO) {
      temp_queda = temp_fin > ahora ? temp_fin - ahora : 0;
      temp = T_PAUSA;
    } else if (temp == T_PAUSA) {
      temp_fin = ahora + temp_queda;
      temp = T_CORRIENDO;
    }
  }
}

// ============================================================
//  El permiso: Claude pide algo y lo contestas en la placa
// ============================================================
const int PM_BOT_Y = 246, PM_BOT_H = 40;

void permiso_toque(int x, int y) {
  if (y < PM_BOT_Y || y >= PM_BOT_Y + PM_BOT_H) return;      // solo los dos botones
  bool ok = x >= 120;
  char b[96];
  snprintf(b, sizeof(b), "{\"ev\":\"permiso\",\"id\":\"%s\",\"ok\":%d}", perm_id.c_str(), ok ? 1 : 0);
  pc_enviar_linea(b);
  perm_activo = false;
  if (ok) SONAR(S_SALTO); else SONAR(S_CLIC);
  aviso(ok ? T("Permiso concedido") : T("Permiso denegado"));
}

void dibujar_permiso() {
  if (!perm_activo) return;
  if (perm_prox_pitido == 0) {                     // te avisa una vez, al llegar
    SONAR_SIEMPRE(S_ESPERA);
    perm_prox_pitido = 1;
  }
  oscurecer();
  float k = 0.5f + 0.5f * sinf(ahora / 380.0f);
  g.fillRoundRect(10, 40, 220, 262, 12, SUP);
  g.drawRoundRect(10, 40, 220, 262, 12, mezclar(BORDE, AMARILLO, k));
  texto_centrado(g, T("PERMISO"), 52, L, AMARILLO);
  g.setTextDatum(top_center);
  g.setFont(F_TXT);
  g.setTextColor(TXT);
  g.drawString(T("Claude quiere usar ") + perm_herr, 120, 84);
  g.setFont(F_PEQ);
  g.setTextColor(TXT3);
  g.drawString(recortar(T("en ") + perm_proy, 200), 120, 102);
  // lo que quiere hacer, tal cual (en monoespaciada, como en la terminal)
  g.fillRoundRect(20, 122, 200, 110, 8, FONDO);
  String lineas[6];
  int n = envolver(perm_det, 31, lineas, 6);
  g.setTextDatum(top_left);
  g.setTextColor(TXT2);
  for (int i = 0; i < n; i++) texto_mixto(g, lineas[i], 28, 130 + i * 16, F_PEQ, top_left);
  boton(20, PM_BOT_Y, 96, PM_BOT_H, T("Denegar"), false);
  boton(124, PM_BOT_Y, 96, PM_BOT_H, T("Permitir"), true);
  g.setTextDatum(top_left);
}

// ============================================================
//  La cortina (se dibuja encima de todo)
// ============================================================
// Un interruptor de la cortina: botón cuadrado con su icono y el nombre debajo
void interruptor(int i, int oy, bool activo, const char *const *ico, int filas, int ancho, const char *nombre) {
  int x = (i % 4) * 60 + 8, y = oy + (i < 4 ? CT_FILA1 : CT_FILA2);
  g.fillRoundRect(x, y, 44, CT_BOT, 10, activo ? SUP2 : SUP);
  g.drawRoundRect(x, y, 44, CT_BOT, 10, activo ? TXT3 : BORDE);
  patron(g, ico, filas, x + 22 - ancho, y + 20 - filas, 2, activo ? TXT : TXT3);
  g.setFont(F_PEQ);
  g.setTextDatum(top_center);
  g.setTextColor(activo ? TXT : TXT3);
  g.drawString(nombre, x + 22, y + CT_BOT + 4);
}

void deslizador(int y, const char *const *ico, int filas, int pct, int minimo) {
  patron(g, ico, filas, 12, y - filas, 2, TXT2);
  int xb = 40, wb = 180;
  g.fillRoundRect(xb, y - 3, wb, 6, 3, SUP2);
  int xk = xb + wb * (pct - minimo) / (100 - minimo);
  if (xk > xb) g.fillRoundRect(xb, y - 3, xk - xb, 6, 3, TXT2);
  g.fillRoundRect(xk - 5, y - 8, 10, 16, 4, TXT);
}

void dibujar_cortina() {
  if (!cortina_arrastre) {                   // ir hacia abierta/cerrada con suavizado
    cortina += (cortina_obj - cortina) * 0.32f;
    if (fabsf(cortina_obj - cortina) < 0.004f) cortina = cortina_obj;
  }
  if (cortina <= 0.001f) return;
  float e = 1 - (1 - cortina) * (1 - cortina);   // "ease out", como en el móvil
  int oy = (int)((e - 1) * 320);
  g.fillRect(0, oy, 240, 320, FONDO);
  g.drawFastHLine(0, oy + 319, 240, BORDE);

  // hora grande y fecha
  long s = hora_seg();
  char hora[8];
  if (s >= 0) snprintf(hora, sizeof(hora), "%02ld:%02ld", s / 3600, (s / 60) % 60);
  else strcpy(hora, "--:--");
  texto_pixel(g, hora, 3, (oy + 14) / L, L, TXT);
  g.setTextDatum(top_right);
  g.setFont(F_PEQ);
  g.setTextColor(TXT2);
  g.drawString(pc_dia.length() ? pc_dia : String("--"), 192, oy + 14);
  if (pc_bat >= 0) {
    g.setTextColor(pc_carga ? VERDE : TXT3);
    g.drawString(String(pc_bat) + " %" + (pc_carga ? T(" · cargando") : ""), 192, oy + 30);
  }
  g.fillRoundRect(200, oy + 10, 32, 32, 8, SUP);                 // ⚙ Ajustes
  g.drawRoundRect(200, oy + 10, 32, 32, 8, BORDE);
  patron(g, ICO_AJUSTES, 7, 209, oy + 19, 2, TXT2);

  // los 8 interruptores
  bool t_activo = temp != T_PARADO || pomo != POMO_NO;
  interruptor(0, oy, !audio_mudo, audio_mudo ? ICO_ALTAVOZ_NO : ICO_ALTAVOZ, 7, 9, T("Sonido"));
  interruptor(1, oy, !mic_mudo, ICO_MICRO, 8, 7, T("Micro"));
  interruptor(2, oy, no_molestar, ICO_DND, 7, 7, T("No molestar"));
  interruptor(3, oy, durmiendo, ICO_ZZ, 6, 7, T("Dormir"));
  interruptor(4, oy, t_activo, ICO_RELOJ, 7, 7, T("Tiempo"));
  interruptor(5, oy, oye_on, ICO_ONDA, 5, 9, T("Oye Clawd"));
  interruptor(6, oy, habla_on, ICO_HABLA, 7, 7, T("Habla"));
  interruptor(7, oy, carteles_on, ICO_CAMPANA, 6, 7, T("Carteles"));
  if (mic_mudo) patron(g, ICO_TACHON, 8, 60 + 8 + 15, oy + CT_FILA1 + 12, 2, TXT3);

  // brillo y volumen
  deslizador(oy + CT_BRILLO, ICO_SOL, 5, brillo_usuario, 10);
  deslizador(oy + CT_VOLUMEN, ICO_ALTAVOZ, 7, volumen, 0);

  // el último aviso (tócalo: la lista entera)
  int ya = oy + CT_AVISO;
  tarjeta(8, ya, 224, 34);
  g.setTextDatum(middle_left);
  if (n_notifs == 0) {
    g.setFont(F_PEQ);
    g.setTextColor(TXT3);
    g.setTextDatum(middle_center);
    g.drawString(T("Sin avisos"), 120, ya + 17);
  } else {
    const Notif &nt = notifs[0];
    g.fillRect(18, ya + 14, 6, 6, nt.tipo == 0 ? VERDE : (nt.tipo == 1 ? AMARILLO : TXT2));
    g.setFont(F_PEQ);
    g.setTextColor(TXT);
    g.drawString(recortar(nt.titulo + " · " + duracion(ahora - nt.t), n_notifs > 1 ? 160 : 196), 32, ya + 17);
    if (n_notifs > 1) {
      g.setTextDatum(middle_right);
      g.setTextColor(TXT3);
      g.drawString("+" + String(n_notifs - 1) + " >", 224, ya + 17);
    }
  }

  // dock: las 5 pantallas
  const char *nombres[N_PANTALLAS] = {"Clawd", "Claude", T("Voz"), T("Vault"), T("Juegos")};
  int yd = oy + CT_DOCK;
  for (int i = 0; i < N_PANTALLAS; i++) {
    int cxs = i * DOCK_W + DOCK_W / 2;
    if (i == (int)pantalla) g.fillRoundRect(cxs - DOCK_W / 2 + 2, yd, DOCK_W - 4, 40, 8, SUP2);
    uint16_t c = i == (int)pantalla ? TXT : TXT2;
    switch (i) {
      case 0: { Pose m; m.p = 1; m.x = cxs; m.suelo = yd + 22; m.sombra = 0; dibujar_clawd(g, m); break; }
      case 1: patron(g, SPIN_2, 5, cxs - 7, yd + 6, 3, CLAWD); break;
      case 2: patron(g, ICO_TERM, 5, cxs - 8, yd + 8, 2, c); break;
      case 3: patron(g, ICO_GALAX, 5, cxs - 9, yd + 8, 2, c); break;
      case 4: patron(g, ICO_MANDO, 5, cxs - 11, yd + 8, 2, c); break;
    }
    g.setFont(F_PEQ);
    g.setTextDatum(top_center);
    g.setTextColor(c);
    g.drawString(nombres[i], cxs, yd + 25);
  }
  g.fillRoundRect(100, oy + 311, 40, 4, 2, TXT3);   // el asa
  g.setTextDatum(top_left);
}

// ---- Ajustes: el idioma ----
void poner_idioma(Idioma i) {
  idioma = i;
  letras_para(i);
  ajustes.putUChar("idioma", i);
  char b[24];
  snprintf(b, sizeof(b), "idioma:%s", CODIGO_IDIOMA[i]);
  pc_enviar(b);
}

void ajustes_toque(int x, int y) {
  SONAR(S_CLIC);
  if (y < 64 && x > 190) { ajustes_abierto = false; return; }   // la X
  if (y >= 96 && y < 96 + 4 * 44) poner_idioma((Idioma)((y - 96) / 44));
}

void dibujar_ajustes() {
  if (!ajustes_abierto) return;
  g.fillRect(0, BARRA_H, 240, 320 - BARRA_H, FONDO);
  g.setFont(F_GRANDE);
  g.setTextDatum(middle_left);
  g.setTextColor(TXT);
  g.drawString(T("Ajustes"), 14, 44);
  patron(g, ICO_CERRAR, 5, 212, 39, 2, TXT2);
  g.setFont(F_PEQ);
  g.setTextColor(TXT3);
  g.drawString(T("Idioma"), 14, 80);
  for (int i = 0; i < 4; i++) {
    int y = 96 + i * 44;
    bool sel = i == (int)idioma;
    g.fillRoundRect(8, y, 224, 38, 8, sel ? SUP2 : SUP);
    g.drawRoundRect(8, y, 224, 38, 8, sel ? TXT3 : BORDE);
    // cada idioma con una letra que tenga sus caracteres
    g.setFont(i == ZH ? (const lgfx::IFont *)&fonts::efontCN_14 : (const lgfx::IFont *)&fonts::efontJA_14);
    g.setTextColor(sel ? TXT : TXT2);
    g.drawString(NOMBRE_IDIOMA[i], 22, y + 19);
    if (sel) patron(g, ICO_HECHA, 4, 206, y + 15, 2, TXT);
  }
  g.setTextDatum(top_left);
}

// ---- la lista completa de avisos ----
void avisos_toque(int x, int y) {
  SONAR(S_CLIC);
  if (y < 64 && x > 190) { avisos_abierto = false; return; }                        // la X
  if (y >= 278) { n_notifs = 0; avisos_abierto = false; aviso(T("Avisos borrados")); return; }   // "Borrar todo"
  if (y >= 70 && y < 70 + n_notifs * 34) {                                          // un aviso de Claude: ver el progreso
    if (notifs[(y - 70) / 34].tipo != 2) ir_a_pantalla_n(PANT_CLAUDE, 1);
    avisos_abierto = false;
  }
}

void dibujar_avisos() {
  if (!avisos_abierto) return;
  g.fillRect(0, BARRA_H, 240, 320 - BARRA_H, FONDO);
  g.setFont(F_GRANDE);
  g.setTextDatum(middle_left);
  g.setTextColor(TXT);
  g.drawString(T("Avisos"), 14, 44);
  patron(g, ICO_CERRAR, 5, 212, 39, 2, TXT2);
  for (int i = 0; i < n_notifs; i++) {
    const Notif &nt = notifs[i];
    int y = 70 + i * 34;
    tarjeta(8, y, 224, 30);
    g.fillRect(18, y + 12, 6, 6, nt.tipo == 0 ? VERDE : (nt.tipo == 1 ? AMARILLO : TXT2));
    g.setFont(F_PEQ);
    g.setTextDatum(middle_left);
    g.setTextColor(TXT);
    g.drawString(recortar(nt.titulo, 130), 32, y + 15);
    g.setTextDatum(middle_right);
    g.setTextColor(TXT3);
    g.drawString(T("hace ") + duracion(ahora - nt.t), 224, y + 15);
  }
  if (n_notifs == 0) {
    g.setFont(F_PEQ);
    g.setTextDatum(middle_center);
    g.setTextColor(TXT3);
    g.drawString(T("Sin avisos"), 120, 120);
  }
  boton(8, 278, 224, 32, T("Borrar todo"), false);
  g.setTextDatum(top_left);
}

// ============================================================
//  La pantalla de los juegos
// ============================================================
void fotograma_juegos() {
  interaccion();                    // jugando no se duerme
  juego_actualizar(ahora);
  g.fillScreen(FONDO);
  juego_dibujar(g, ahora);
  if (fase == F_FIN && juego != J_MENU) led_pulso(800, 30, 4, 2);
  else led(3, 1, 0);
  brillo_obj = 140;
}

// ============================================================
//  Enviar el fotograma a la pantalla (con la transición, si toca)
// ============================================================
void presentar() {
  float t = trans_dir ? (ahora - trans_ini) / (float)TRANS_MS : 1;
  if (t >= 1) {
    trans_dir = 0;
    g.pushSprite(0, 0);
    return;
  }
  float e = 1 - powf(1 - t, 3);             // ease-out: rápido al principio, suave al final
  int off = (int)(e * 240);
  // el contenido se desliza; la barra de estado se queda quieta
  tft.setClipRect(0, BARRA_H, 240, 320 - BARRA_H);
  previa.pushSprite(-trans_dir * off, 0);
  g.pushSprite(trans_dir * (240 - off), 0);
  tft.setClipRect(0, 0, 240, BARRA_H);
  g.pushSprite(0, 0);
  tft.clearClipRect();
}

// ============================================================
void setup() {
  Serial.setRxBufferSize(16384);  // las respuestas de Claude y la voz de Clawd (ráfagas de ~13 KB)
  Serial.setTxBufferSize(8192);   // y la voz ocupa bastante
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);       // botón BOOT
  ajustes.begin("clawd");
  juegos_iniciar();
  audio_mudo = ajustes.getBool("mudo", false);
  mic_mudo = ajustes.getBool("mic", false);
  brillo_usuario = ajustes.getInt("brillo", 100);
  volumen = ajustes.getInt("volumen", 70);
  no_molestar = ajustes.getBool("dnd", false);
  habla_on = ajustes.getBool("habla", true);
  oye_on = ajustes.getBool("oye", true);
  carteles_on = ajustes.getBool("carteles", false);   // apagados de serie (KISS)
  idioma = (Idioma)constrain(ajustes.getUChar("idioma", ES), 0, 3);
  letras_para(idioma);
  motivo_arranque = esp_reset_reason();
  miga_anterior = motivo_arranque == ESP_RST_POWERON ? 0 : miga;

  // watchdog: si el loop pasa 8 s sin avanzar, reiniciar (y escribir dónde estaba)
  esp_task_wdt_config_t wdt = {.timeout_ms = 8000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(nullptr);
  randomSeed(esp_random());

  tft.init();
  tft.setBrightness(0);
  tft.fillScreen(TFT_BLACK);

  g.setColorDepth(16);
  g.setPsram(true);
  if (!g.createSprite(240, 320)) {
    tft.setBrightness(150);
    tft.setTextColor(TFT_RED);
    tft.drawString("Sin PSRAM para el lienzo", 10, 10);
    while (true) delay(1000);
  }

  // segundo lienzo para las transiciones (si no hay memoria, se cambia sin animar)
  previa.setColorDepth(16);
  previa.setPsram(true);
  hay_previa = previa.createSprite(240, 320) != nullptr;

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(20);            // que un I2C atascado no congele todo
  tactil_iniciar();
  audio_iniciar();
  audio_volumen(volumen);

  ahora = millis();
  ultima_interaccion = ahora;
  construir_animaciones();
  animar(anim_al_azar(false), ahora);   // la "entrada", como al abrir Claude Code
  SONAR(S_HOLA);
}

void loop() {
  esp_task_wdt_reset();           // "sigo vivo"
  ahora = millis();
  miga = 1; leer_tactil();
  miga = 2; leer_boton();
  miga = 3; leer_micro();
  miga = 4; leer_pc();
  miga = 5; actualizar_voz();
  miga = 6; actualizar_tiempos(); temp_actualizar();
  miga = 7 + pantalla;            // 7 mascota, 8 terminal, 9 galaxia
  if (pantalla == PANT_MASCOTA) fotograma();
  else if (pantalla == PANT_CLAUDE) fotograma_claude();
  else if (pantalla == PANT_GALAXIA) fotograma_galaxia();
  else if (pantalla == PANT_JUEGOS) fotograma_juegos();
  else fotograma_terminal();

  // capas comunes, encima de cualquier pantalla
  dibujar_reloj();
  dibujar_avisos();
  dibujar_ajustes();
  dibujar_barra_arriba();
  dibujar_indicador();
  dibujar_alerta();
  dibujar_permiso();
  dibujar_cortina();
  presentar();

  if (alerta == AL_LISTO)  led_pulso(1400, 0, 40, 6);
  if (alerta == AL_ESPERA) led_pulso(1000, 45, 30, 0);
  if (alerta == AL_TIEMPO) led_pulso(500, 45, 30, 0);
  if (perm_activo) led_pulso(700, 45, 30, 0);
  led_aplicar();
  float objetivo = brillo_obj * brillo_usuario / 100.0f;
  brillo += (objetivo - brillo) * 0.08f;
  tft.setBrightness((int)brillo);

  if (pedir_captura) { pedir_captura = false; enviar_captura(g, (int)pantalla); }
  miga = 10;
  uint32_t dt = millis() - ahora;   // ~25 fotogramas por segundo
  if (dt < 40) delay(40 - dt);
}
