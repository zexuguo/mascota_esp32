#pragma once
// ------------------------------------------------------------
// idiomas.h — la placa en español, inglés, chino y catalán
//
// Todos los textos del código están escritos en español y pasan por T():
//     aviso(T("Sonido: apagado"));
// T() busca el texto en la tabla y devuelve la traducción del idioma elegido.
// Si no lo encuentra, devuelve el español (así nunca sale vacío).
// Para añadir un texto: ponerlo con T("...") y añadir su fila aquí.
// ------------------------------------------------------------
#include <Arduino.h>

enum Idioma : uint8_t { ES, EN, ZH, CA };
Idioma idioma = ES;
const char *const CODIGO_IDIOMA[] = {"es", "en", "zh", "ca"};
const char *const NOMBRE_IDIOMA[] = {"Español", "English", "中文", "Català"};   // cada uno en su lengua

struct Trad { const char *es, *en, *zh, *ca; };

const Trad TRAD[] = {
  // ---- interfaz ----
  {"  ·  cargando", "  ·  charging", "  ·  充电中", "  ·  carregant"},
  {" · cargando", " · charging", " · 充电中", " · carregant"},
  {" s · toca para cancelar", " s · tap to cancel", " 秒 · 点击取消", " s · toca per cancel·lar"},
  {"Abre el portátil", "Open the laptop", "打开笔记本电脑", "Obre el portàtil"},
  {"Ahora no está trabajando", "Not working right now", "现在没有在工作", "Ara no està treballant"},
  {"Ajustes", "Settings", "设置", "Configuració"},
  {"Avisos", "Notifications", "通知", "Avisos"},
  {"Avisos borrados", "Notifications cleared", "通知已清除", "Avisos esborrats"},
  {"Batería", "Battery", "电池", "Bateria"},
  {"Borrar todo", "Clear all", "全部清除", "Esborra-ho tot"},
  {"Cancelado", "Cancelled", "已取消", "Cancel·lat"},
  {"Cancelar", "Cancel", "取消", "Cancel·la"},
  {"Carteles", "Reminders", "提醒", "Cartells"},
  {"Carteles: activados", "Reminders: on", "提醒:开", "Cartells: activats"},
  {"Carteles: apagados", "Reminders: off", "提醒:关", "Cartells: apagats"},
  {"Claude Code: puede leer y", "Claude Code: it can read", "Claude Code:它可以读取和", "Claude Code: pot llegir i"},
  {"Claude ha terminado", "Claude is done", "Claude 完成了", "Claude ha acabat"},
  {"Claude pide permiso", "Claude asks for permission", "Claude 请求权限", "Claude demana permís"},
  {"Claude quiere usar ", "Claude wants to use ", "Claude 想使用 ", "Claude vol fer servir "},
  {"Claude te necesita", "Claude needs you", "Claude 需要你", "Claude et necessita"},
  {"Clawd habla", "Clawd speaks", "Clawd 会说话", "Clawd parla"},
  {"Clawd no habla", "Clawd is quiet", "Clawd 不说话", "Clawd no parla"},
  {"Conectado al PC", "Connected to the PC", "已连接电脑", "Connectat a l'ordinador"},
  {"Denegar", "Deny", "拒绝", "Denega"},
  {"Descanso", "Break", "休息", "Descans"},
  {"Descanso: 5 minutos", "Break: 5 minutes", "休息:5 分钟", "Descans: 5 minuts"},
  {"Dormir", "Sleep", "睡觉", "Dormir"},
  {"El PC no ha contestado", "The PC didn't answer", "电脑没有回应", "L'ordinador no ha respost"},
  {"El companion no está en marcha", "The companion isn't running", "companion 没有运行", "El companion no està en marxa"},
  {"El micro está apagado (baja la cortina)", "The mic is off (pull down the shade)", "麦克风已关闭(下拉控制中心)", "El micro està apagat (baixa la cortina)"},
  {"Empezar", "Start", "开始", "Comença"},
  {"Entendiendo lo que dices…", "Understanding what you said…", "正在理解你说的话…", "Entenent el que dius…"},
  {"Entendiendo…", "Understanding…", "理解中…", "Entenent…"},
  {"Enviando…", "Sending…", "发送中…", "Enviant…"},
  {"Envío en ", "Sending in ", "发送倒计时 ", "Envio en "},
  {"Foco", "Focus", "专注", "Focus"},
  {"Ha tardado ", "Took ", "用时 ", "Ha trigat "},
  {"Habla", "Speak", "说话", "Parla"},
  {"Han pasado ", "Elapsed: ", "已过去 ", "Han passat "},
  {"Idioma", "Language", "语言", "Idioma"},
  {"Juegos", "Games", "游戏", "Jocs"},
  {"Lo que digas se lo paso a", "Whatever you say goes to", "你说的话会交给", "El que diguis ho passo a"},
  {"Mantén pulsado para hablar", "Hold to talk", "按住说话", "Mantén premut per parlar"},
  {"Mantén pulsado y habla.", "Hold and speak.", "按住并说话。", "Mantén premut i parla."},
  {"Memoria", "Memory", "内存", "Memòria"},
  {"Micro", "Mic", "麦克风", "Micro"},
  {"Micro apagado: baja la cortina", "Mic off: pull down the shade", "麦克风已关闭", "Micro apagat: baixa la cortina"},
  {"Micro: apagado", "Mic: off", "麦克风:关", "Micro: apagat"},
  {"Micro: encendido", "Mic: on", "麦克风:开", "Micro: encès"},
  {"Mira la pantalla del PC", "Check the PC screen", "看看电脑屏幕", "Mira la pantalla de l'ordinador"},
  {"No molestar", "Quiet", "勿扰", "No molestis"},
  {"No molestar: apagado", "Quiet mode: off", "勿扰:关", "No molestis: apagat"},
  {"No molestar: solo alarmas y permisos", "Quiet: only alarms and permissions", "勿扰:只有闹钟和权限", "No molestis: només alarmes i permisos"},
  {"Oye Clawd", "Hey Clawd", "嘿 Clawd", "Ei Clawd"},
  {"Oye Clawd: apagado", "Hey Clawd: off", "嘿 Clawd:关", "Ei Clawd: apagat"},
  {"Oye Clawd: escuchando", "Hey Clawd: listening", "嘿 Clawd:开", "Ei Clawd: escoltant"},
  {"PC desconectado", "PC disconnected", "电脑已断开", "Ordinador desconnectat"},
  {"Para oírte necesito el PC", "I need the PC to hear you", "需要电脑才能听你说话", "Per sentir-te necessito l'ordinador"},
  {"Pausar", "Pause", "暂停", "Pausa"},
  {"Permiso concedido", "Permission granted", "已允许", "Permís concedit"},
  {"Permiso denegado", "Permission denied", "已拒绝", "Permís denegat"},
  {"Permitir", "Allow", "允许", "Permet"},
  {"Pide permiso en la terminal", "Asking for permission in the terminal", "在终端里请求权限", "Demana permís al terminal"},
  {"Pomodoro", "Pomodoro", "番茄钟", "Pomodoro"},
  {"Pomodoro cancelado", "Pomodoro cancelled", "番茄钟已取消", "Pomodoro cancel·lat"},
  {"Pomodoro terminado", "Pomodoro finished", "番茄钟结束", "Pomodoro acabat"},
  {"Pomodoro: 25 min de foco", "Pomodoro: 25 min of focus", "番茄钟:专注 25 分钟", "Pomodoro: 25 min de focus"},
  {"Se acabó el tiempo", "Time's up", "时间到", "S'ha acabat el temps"},
  {"Seguir", "Resume", "继续", "Continua"},
  {"Sin PC: el companion no está en marcha", "No PC: the companion isn't running", "没有电脑:companion 没有运行", "Sense ordinador: el companion no està en marxa"},
  {"Sin avisos", "No notifications", "没有通知", "Sense avisos"},
  {"Sin conexión con el PC", "No connection to the PC", "未连接电脑", "Sense connexió amb l'ordinador"},
  {"Sin lista de tareas", "No task list", "没有任务列表", "Sense llista de tasques"},
  {"Sin pomodoro", "No pomodoro", "取消番茄钟", "Sense pomodoro"},
  {"Sonido", "Sound", "声音", "So"},
  {"Sonido: apagado", "Sound: off", "声音:关", "So: apagat"},
  {"Sonido: encendido", "Sound: on", "声音:开", "So: encès"},
  {"Tapa cerrada", "Lid closed", "盖子已合上", "Tapa tancada"},
  {"Tapa cerrada · no se suspende", "Lid closed · won't sleep", "盖子已合上 · 不会休眠", "Tapa tancada · no se suspèn"},
  {"Te escucho", "I'm listening", "我在听", "T'escolto"},
  {"Te escucho…", "Listening…", "我在听…", "T'escolto…"},
  {"Te necesita: pide permiso", "Needs you: asking permission", "需要你:请求权限", "Et necessita: demana permís"},
  {"Te pide permiso en la terminal.", "It asks for permission in the terminal.", "它在终端里请求权限。", "Et demana permís al terminal."},
  {"Temporizador", "Timer", "计时器", "Temporitzador"},
  {"Temporizador de ", "Timer: ", "计时器 ", "Temporitzador de "},
  {"Temporizador: ", "Timer: ", "计时器:", "Temporitzador: "},
  {"Terminado hace ", "Finished · ", "已完成 · ", "Acabat fa "},
  {"Tiempo", "Timer", "计时", "Temps"},
  {"Toca descansar 5 minutos", "Time for a 5-minute break", "休息 5 分钟", "Toca descansar 5 minuts"},
  {"Toca para cerrar", "Tap to close", "点击关闭", "Toca per tancar"},
  {"Toca para decir que sí", "Tap to say yes", "点击表示好的", "Toca per dir que sí"},
  {"Tu vault", "Your vault", "你的笔记库", "La teva vault"},
  {"Vault", "Vault", "笔记", "Vault"},
  {"Voz", "Voice", "语音", "Veu"},
  {"editar archivos de este", "and edit files on this", "编辑这台电脑上的", "editar fitxers d'aquest"},
  {"en ", "in ", "在 ", "a "},
  {"en pausa", "paused", "已暂停", "en pausa"},
  {"ha tardado ", "took ", "用时 ", "ha trigat "},
  {"hace ", "", "", "fa "},
  {"horas", "hours", "小时", "hores"},
  {"minutos", "minutes", "分钟", "minuts"},
  {"ordenador.", "computer.", "文件。", "ordinador."},
  {"por voz · en tu carpeta", "by voice · in your folder", "语音 · 在你的文件夹", "per veu · a la teva carpeta"},
  {"quedan", "left", "剩余", "queden"},
  {"sin sesión", "no session", "无会话", "sense sessió"},
  {"suelta para enviar", "release to send", "松开发送", "deixa anar per enviar"},
  {"toca para parar", "tap to stop", "点击停止", "toca per aturar"},
  {"ÚLTIMOS PASOS", "LAST STEPS", "最近步骤", "DARRERS PASSOS"},
  // ---- ajedrez (nombres de chess.com en cada idioma) ----
  {"AJEDREZ", "CHESS", "国际象棋", "ESCACS"},
  {"PRECISION", "ACCURACY", "准确度", "PRECISIO"},
  {"Brillante", "Brilliant", "妙着", "Brillant"},
  {"Mejor", "Best", "最佳", "Millor"},
  {"Excelente", "Excellent", "优秀", "Excel·lent"},
  {"Buena", "Good", "不错", "Bona"},
  {"Teoría", "Book", "开局库", "Teoria"},
  {"Imprecisión", "Inaccuracy", "不精确", "Imprecisió"},
  {"Error", "Mistake", "错误", "Error"},
  {"Error grave", "Blunder", "严重失误", "Error greu"},
  {"Forzada", "Forced", "唯一着法", "Forçada"},
  {"Partida analizada", "Game analysed", "对局已分析", "Partida analitzada"},
  {"Revisión de partida", "Game Review", "对局复盘", "Revisió de partida"},
  {"Juega en chess.com y aparecerá aquí.", "Play on chess.com and it'll show up here.", "在 chess.com 下一局，就会出现在这里。", "Juga a chess.com i apareixerà aquí."},
  {"Analizar la última", "Analyse the last one", "分析最近一局", "Analitza l'última"},
  {"Analizando en la nube…", "Analysing in the cloud…", "正在云端分析…", "Analitzant al núvol…"},
  {"Victoria", "Win", "胜利", "Victòria"},
  {"Derrota", "Loss", "失败", "Derrota"},
  {"Tablas", "Draw", "和棋", "Taules"},
  {"Tú", "You", "你", "Tu"},
  {"Rival", "Opponent", "对手", "Rival"},
  {"Mis errores", "My mistakes", "我的失误", "Els meus errors"},
  {"Revisar", "Review", "复盘", "Revisa"},
  {"Posición inicial", "Starting position", "初始局面", "Posició inicial"},
  {"Mejor: ", "Best: ", "最佳:", "Millor: "},
  {"solo errores", "mistakes only", "只看失误", "només errors"},
  {"Sobremesa", "Desktop", "台式机", "Sobretaula"},
  {"libre", "idle", "空闲", "lliure"},
  {"encendiendo…", "waking up…", "正在开机…", "encenent…"},
  {"apagado · toca", "off · tap", "已关机 · 点按", "apagat · toca"},
  {"IA parada", "AI stopped", "AI 已停止", "IA aturada"},
  // ---- títulos en píxeles (en chino se pintan con la letra normal) ----
  {"LISTO", "DONE", "完成", "FET"},
  {"PERMISO", "PERMISSION", "权限", "PERMÍS"},
  {"TE NECESITO", "I NEED YOU", "需要你", "ET NECESSITO"},
  {"TIEMPO", "TIME'S UP", "时间到", "TEMPS"},
  {"JUEGOS", "GAMES", "游戏", "JOCS"},
  {"VUELA", "FLY", "飞翔", "VOLA"},
  {"CORRE", "RUN", "奔跑", "CORRE"},
  {"RECORD", "BEST", "最高", "RECORD"},
  {"RECORD!", "NEW RECORD!", "新纪录!", "RECORD!"},
  {"PUNTOS", "SCORE", "得分", "PUNTS"},
  {"FIN", "GAME OVER", "结束", "FI"},
  {"MENU", "MENU", "菜单", "MENU"},
  {"DESLIZA PARA SALIR", "SWIPE TO EXIT", "滑动退出", "LLISCA PER SORTIR"},
  {"TOCA = ALETEAR", "TAP = FLAP", "点击 = 扇翅", "TOCA = ALETEJAR"},
  {"TOCA = SALTAR", "TAP = JUMP", "点击 = 跳", "TOCA = SALTAR"},
  {"TOCA = OTRA VEZ", "TAP = AGAIN", "点击 = 再来", "TOCA = UN ALTRE COP"},
  {"TOCA PARA EMPEZAR", "TAP TO START", "点击开始", "TOCA PER COMENCAR"},
  // ---- bocadillos y carteles ----
  {"Hola!", "Hi!", "你好!", "Hola!"},
  {"Listo!", "Done!", "完成!", "Fet!"},
  {"Descanso!", "Break time!", "休息!", "Descans!"},
  {"A por otra!", "Next one!", "再来一轮!", "Una altra!"},
  {"Eh? Ya voy...", "Huh? Coming...", "嗯?来了...", "Eh? Ja vinc..."},
  {"Te escucho...", "Listening...", "我在听...", "T'escolto..."},
  {"Bebe agua!", "Drink water!", "喝点水!", "Beu aigua!"},
  {"Estirate!", "Stretch!", "伸展一下!", "Estira't!"},
  {"Mira lejos", "Look far away", "看看远处", "Mira lluny"},
  {"Respira hondo", "Breathe deeply", "深呼吸", "Respira fondo"},
  {"Espalda recta!", "Sit up straight!", "坐直!", "Esquena recta!"},
  {"Bien hecho!", "Well done!", "做得好!", "Ben fet!"},
  // ---- bot de trading (trading.h) ----
  {"Bot", "Bot", "交易机器人", "Bot"},
  {"parado", "stopped", "已停止", "aturat"},
  {" abierta", " open", " 持仓", " oberta"},
  {"velas 4 h · 8 días · toca: 1 h", "4 h candles · 8 days · tap: 1 h", "4小时K线 · 8天 · 点击: 1小时", "espelmes 4 h · 8 dies · toca: 1 h"},
  {"velas 1 h · 2 días · toca: 4 h", "1 h candles · 2 days · tap: 4 h", "1小时K线 · 2天 · 点击: 4小时", "espelmes 1 h · 2 dies · toca: 4 h"},
  {"COMPRAR", "BUY", "买入", "COMPRAR"},
  {"VENDER", "SELL", "卖出", "VENDRE"},
  {"ESPERAR", "WAIT", "等待", "ESPERAR"},
  {"Claude decidió", "Claude decided", "Claude 决定", "Claude va decidir"},
  {"Sin datos: ", "No data: ", "没有数据: ", "Sense dades: "},
  {"Cargando…", "Loading…", "加载中…", "Carregant…"},
  {"dentro · ", "in · ", "持仓 · ", "dins · "},
  {"espera a ", "waits for ", "等待 ", "espera a "},
  {"fuera", "out", "空仓", "fora"},
  {"Operaciones", "Trades", "交易记录", "Operacions"},
  {"Tabla", "Table", "表格", "Taula"},
  {"Finanzas", "Finance", "理财", "Finances"},
  {"Pantalla privada", "Private screen", "隐私画面", "Pantalla privada"},
  {"Tu cartera no sale en las capturas", "Your portfolio is never captured", "截图里不会出现你的资产", "La teva cartera no surt a les captures"},
  {"hoy ", "today ", "今天 ", "avui "},
  {"hoy · la gráfica se va llenando", "today · the chart fills up over time", "今天 · 图表会慢慢填满", "avui · el gràfic s'anirà omplint"},
  {"últimos ", "last ", "最近 ", "últims "},
  {" días", " days", " 天", " dies"},
  {"Invertido", "Invested", "已投资", "Invertit"},
  {"Efectivo", "Cash", "现金", "Efectiu"},
  {"Ganancia", "Gain", "收益", "Guany"},
  {"En tu PC, en la carpeta clawd:", "On your PC, in the clawd folder:", "在电脑的 clawd 文件夹里:", "Al teu PC, a la carpeta clawd:"},
  {" cerradas · ", " closed · ", " 已平仓 · ", " tancades · "},
  {"Par", "Pair", "交易对", "Parell"},
  {"Fecha", "Date", "日期", "Data"},
  {"abierta", "open", "持仓中", "oberta"},
  {"esperando", "waiting", "等待中", "esperant"},
  {"revisar", "check", "需检查", "revisar"},
  {"velas de ", "candles ", "K线 ", "espelmes de "},
  {"bloqueado · 30 d ", "blocked · 30 d ", "禁止买入 · 30天 ", "bloquejat · 30 d "},
  {"Total", "Total", "总计", "Total"},
  {"% aciertos", "% wins", "% 胜率", "% encerts"},
};

const char *T(const char *es) {
  if (idioma == ES || !es) return es;
  for (const Trad &t : TRAD) {
    if (strcmp(t.es, es) != 0) continue;
    const char *r = idioma == EN ? t.en : idioma == ZH ? t.zh : t.ca;
    return r ? r : es;
  }
  return es;
}

// ¿Tiene letras que no son ASCII? (chino, tildes...): entonces no vale la letra de píxeles
bool no_ascii(const char *t) {
  for (; *t; t++) if ((uint8_t)*t >= 0x80) return true;
  return false;
}

// Ancho en "columnas" de terminal: el chino ocupa 2, el resto 1
int columnas_de(const char *t, int bytes = -1) {
  int c = 0;
  for (int i = 0; t[i] && (bytes < 0 || i < bytes);) {
    uint8_t b = t[i];
    int n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
    uint32_t cp = n == 1 ? b : n == 2 ? ((b & 0x1F) << 6) | (t[i + 1] & 0x3F)
                : n == 3 ? ((b & 0x0F) << 12) | ((t[i + 1] & 0x3F) << 6) | (t[i + 2] & 0x3F) : 0x10000;
    c += cp >= 0x2E80 ? 2 : 1;
    i += n;
  }
  return c;
}
