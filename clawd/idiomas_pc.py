"""Idiomas del companion de Clawd: español, inglés, chino y catalán.

- ORDENES:   lo que Clawd hace solo cuando se lo dices (en cada idioma)
- MSG:       los mensajes que el companion manda a la placa o dice en voz alta
- segundos_de(): "diez minutos" / "ten minutes" / "deu minuts" / "十分钟" -> 600
- fecha(), VOCES, LLAMADAS ("Oye / Hey / Ei / 嘿 Clawd", cada una en su idioma), PISTA
"""
import datetime
import re
from pathlib import Path

IDIOMAS = ("es", "en", "zh", "ca")

# Voces de Piper (en local). Se descargan con:  python -m piper.download_voices <nombre>
VOCES = {
    "es": "es_ES-sharvard-medium",
    "en": "en_US-lessac-medium",
    "zh": "zh_CN-huayan-medium",
    "ca": "ca_ES-upc_ona-medium",
}
CARPETA_VOCES = Path.home() / ".local/share/clawd/voces"

# Vocabulario que se le da a Whisper para que acierte palabras raras (en el idioma elegido).
# OJO: NUNCA poner aquí "Oye Clawd". Con ruido (sin palabras), Whisper "alucina" y escribe
# la pista tal cual: escribía "Oye Clawd." y Clawd se ponía a escuchar solo.
PISTA = {
    "es": "Claude, pomodoro, temporizador, archivo, carpeta, proyecto, commit.",
    "en": "Claude, pomodoro, timer, file, folder, project, commit.",
    "zh": "Claude，番茄钟，计时器，文件，文件夹，项目。",
    "ca": "Claude, pomodoro, temporitzador, fitxer, carpeta, projecte, commit.",
}

# La llamada: SOLO la de cada idioma, completa ("Clawd" a secas o "Hola Claude" no valen).
# Se mira con el idioma que ha detectado Whisper. Las variantes son cómo lo suele escribir.
_NOMBRE = r"(clawd|claude|clod|cloud|claud|clow|glod|klod)"
_SEP = r"[\s,.!¡?¿]*"            # Whisper pone comas y signos: "¡Oye, Claude! ¡Baila!"
LLAMADAS = {
    "es": re.compile(r"^" + _SEP + r"(oye|oie)" + _SEP + _NOMBRE + r"\b" + _SEP, re.I),
    "en": re.compile(r"^" + _SEP + r"(hey|hay|hei)( a)?" + _SEP + _NOMBRE + r"\b" + _SEP, re.I),   # "Hay a Cloud"
    "ca": re.compile(r"^" + _SEP + r"(ei|ey|hei|hey)" + _SEP + _NOMBRE + r"\b" + _SEP, re.I),
    "zh": re.compile(r"^[\s,，、。!！]*(嘿|黑|嗨|hey)[\s,，、。!！]*(" + _NOMBRE[1:-1] + r"|克劳德|克劳迪|可劳德|克劳)[\s,，、。.!！?？]*", re.I),
}


INSTRUCCIONES_IDIOMA = (
    "Responde en el mismo idioma en que te hable: español, inglés, chino simplificado o catalán "
    "(si no estás seguro, en {ui}).")
NOMBRE = {"es": "español", "en": "inglés", "zh": "chino simplificado", "ca": "catalán"}

MSG = {
    "muy_corto": {"es": "Muy corto: mantén pulsado mientras hablas", "en": "Too short: hold while you speak",
                  "zh": "太短了：说话时请按住", "ca": "Massa curt: mantén premut mentre parles"},
    "cargando": {"es": "Whisper aún se está cargando, espera un poco", "en": "Whisper is still loading, wait a moment",
                 "zh": "Whisper 还在加载，请稍等", "ca": "Whisper encara s'està carregant, espera una mica"},
    "no_entendido": {"es": "No te he entendido", "en": "I didn't understand you", "zh": "我没听懂",
                     "ca": "No t'he entès"},
    "sigue": {"es": "Claude sigue con lo anterior", "en": "Claude is still on the previous task",
              "zh": "Claude 还在处理上一个任务", "ca": "Claude encara està amb l'anterior"},
    "timer_cancel": {"es": "Temporizador cancelado", "en": "Timer cancelled", "zh": "计时器已取消",
                     "ca": "Temporitzador cancel·lat"},
    "timer_de": {"es": "Temporizador de {d}", "en": "Timer: {d}", "zh": "计时器 {d}", "ca": "Temporitzador de {d}"},
    "son_las": {"es": "Son las {h}", "en": "It's {h}", "zh": "现在是 {h}", "ca": "Són les {h}"},
    "conv_nueva": {"es": "Conversación nueva con Claude", "en": "New conversation with Claude",
                   "zh": "和 Claude 开始新对话", "ca": "Conversa nova amb Claude"},
    "claude_no": {"es": "Claude no ha contestado", "en": "Claude didn't answer", "zh": "Claude 没有回答",
                  "ca": "Claude no ha respost"},
    "problema": {"es": "Claude ha tenido un problema", "en": "Claude ran into a problem", "zh": "Claude 遇到了问题",
                 "ca": "Claude ha tingut un problema"},
    "sin_permiso": {"es": "Sin permiso: ", "en": "No permission: ", "zh": "没有权限：", "ca": "Sense permís: "},
    "listo_voz": {"es": "Listo. Claude ha terminado en {p}.", "en": "Done. Claude has finished in {p}.",
                  "zh": "完成了。Claude 在 {p} 里完成了工作。", "ca": "Fet. Claude ha acabat a {p}."},
    "permiso_voz": {"es": "Claude necesita permiso para usar {h}.", "en": "Claude needs permission to use {h}.",
                    "zh": "Claude 需要使用 {h} 的权限。", "ca": "Claude necessita permís per fer servir {h}."},
    "tu_proyecto": {"es": "tu proyecto", "en": "your project", "zh": "你的项目", "ca": "el teu projecte"},
    # pasos de Claude que se ven en el monitor
    "leo": {"es": "Leo", "en": "Read", "zh": "读取", "ca": "Llegeixo"},
    "edito": {"es": "Edito", "en": "Edit", "zh": "编辑", "ca": "Edito"},
    "escribo": {"es": "Escribo", "en": "Write", "zh": "写入", "ca": "Escric"},
    "busco": {"es": "Busco", "en": "Search", "zh": "搜索", "ca": "Cerco"},
    "internet": {"es": "Busco en internet: ", "en": "Web search: ", "zh": "网上搜索：", "ca": "Cerco a internet: "},
    "web": {"es": "Leo una web", "en": "Read a web page", "zh": "读取网页", "ca": "Llegeixo una web"},
    "tareas": {"es": "Organizo las tareas", "en": "Plan the tasks", "zh": "整理任务", "ca": "Organitzo les tasques"},
}


def msg(clave: str, idioma: str, **k) -> str:
    t = MSG[clave].get(idioma) or MSG[clave]["es"]
    return t.format(**k)


# ------------------------------------------------------------------ órdenes para Clawd
# (patrón, orden para la placa, lo que contesta). Se miran en orden: lo más concreto, antes.
ORDENES = {
    "es": [
        (r"\bpomodoro\b", "pomodoro", "Pomodoro de 25 min en marcha"),
        (r"\bbail", "baila", "¡A bailar!"),
        (r"\b(duermete|a dormir|buenas noches)\b", "dormir", "Buenas noches"),
        (r"\b(despierta|despiertate)\b", "despertar", "Ya estoy despierto"),
        (r"\b(silencio|callate)\b", "mudo", "Sonido apagado"),
        (r"\b(activa el sonido|pon el sonido|con sonido)\b", "sonido", "Sonido encendido"),
        (r"\b(a volar|juega a volar|vuela)\b", "vuela", "¡A volar!"),
        (r"\b(a correr|juega a correr|corre)\b", "corre", "¡A correr!"),
        (r"\b(juega|jugar|juegos|vamos a jugar)\b", "juegos", "¡A jugar!"),
        (r"\b(como va|progreso|que hace claude|como vas)\b", "claude", "Así va Claude"),
        (r"\b(salta|un salto)\b", "anim:jump", "¡Hop!"),
        (r"\b(gira|da una vuelta|date la vuelta|vuelta)\b", "anim:turn", "Vueltecita"),
        (r"\b(guina|guino|un guino)\b", "anim:wink", ";)"),
        (r"\b(estornuda|achis)\b", "anim:sneeze", "¡Achís!"),
        (r"\b(escondete|cucu|esconde)\b", "anim:peekaboo", "¡Cucú!"),
        (r"\b(celebra|celebralo|fiesta)\b", "anim:celebrate", "¡Yuju!"),
        (r"\b(mira|mira alrededor)\b", "anim:look", "Mirando..."),
        (r"\b(asomate|asoma)\b", "anim:peek", "¿Hola?"),
        (r"\b(cae|caete|tirate)\b", "anim:drop", "¡Plof!"),
        (r"\b(moneda|haz la moneda)\b", "anim:coin-hop", "¡Tachán!"),
        (r"\b(anda|camina|pasea)\b", "anim:waddle", "Paseíto"),
        (r"\b(zapatea|baile de pies)\b", "anim:tap", "Tap tap"),
        (r"\b(abre la terminal|terminal)\b", "terminal", "Aquí la tienes"),
        (r"\b(galaxia|nebula|universo|constelacion)\b", "galaxia", "Tu vault, en estrellas"),
        (r"\b(vuelve|mascota)\b", "mascota", "Vuelvo"),
    ],
    "en": [
        (r"\bpomodoro\b", "pomodoro", "Pomodoro: 25 min started"),
        (r"\bdanc", "baila", "Let's dance!"),
        (r"\b(go to sleep|sleep|good night)\b", "dormir", "Good night"),
        (r"\bwake up\b", "despertar", "I'm awake"),
        (r"\b(unmute|sound on|turn on the sound)\b", "sonido", "Sound on"),
        (r"\b(mute|be quiet|silence)\b", "mudo", "Sound off"),
        (r"\b(fly|play fly)\b", "vuela", "Let's fly!"),
        (r"\b(run|play run)\b", "corre", "Let's run!"),
        (r"\b(let s play|lets play|play|games)\b", "juegos", "Let's play!"),
        (r"\b(how is it going|progress|what is claude doing|how are you doing)\b", "claude", "Here's Claude"),
        (r"\bjump\b", "anim:jump", "Hop!"),
        (r"\b(turn around|spin|spin around)\b", "anim:turn", "Spinning"),
        (r"\bwink\b", "anim:wink", ";)"),
        (r"\bsneeze\b", "anim:sneeze", "Achoo!"),
        (r"\bhide\b", "anim:peekaboo", "Peekaboo!"),
        (r"\b(celebrate|party)\b", "anim:celebrate", "Yay!"),
        (r"\blook around\b", "anim:look", "Looking..."),
        (r"\b(walk|go for a walk)\b", "anim:waddle", "Strolling"),
        (r"\b(open the terminal|terminal)\b", "terminal", "Here it is"),
        (r"\b(galaxy|nebula|universe)\b", "galaxia", "Your vault, as stars"),
        (r"\b(come back|go home|pet)\b", "mascota", "Coming back"),
    ],
    "ca": [
        (r"\bpomodoro\b", "pomodoro", "Pomodoro de 25 min en marxa"),
        (r"\bball", "baila", "A ballar!"),
        (r"\b(dorm|a dormir|bona nit)\b", "dormir", "Bona nit"),
        (r"\bdesperta", "despertar", "Ja estic despert"),
        (r"\b(activa el so|posa el so|amb so)\b", "sonido", "So encès"),
        (r"\b(silenci|calla)\b", "mudo", "So apagat"),
        (r"\b(a volar|vola)\b", "vuela", "A volar!"),
        (r"\b(a correr|corre)\b", "corre", "A córrer!"),
        (r"\b(juga|jugar|jocs|juguem)\b", "juegos", "A jugar!"),
        (r"\b(com va|progres|que fa claude)\b", "claude", "Així va Claude"),
        (r"\bsalta\b", "anim:jump", "Hop!"),
        (r"\b(gira|fes una volta|volta)\b", "anim:turn", "Voltereta"),
        (r"\bullet\b", "anim:wink", ";)"),
        (r"\besternuda", "anim:sneeze", "Atxim!"),
        (r"\bamaga", "anim:peekaboo", "Cucut!"),
        (r"\b(celebra|festa)\b", "anim:celebrate", "Visca!"),
        (r"\b(passeja|camina)\b", "anim:waddle", "Passejant"),
        (r"\b(obre el terminal|terminal)\b", "terminal", "Aquí el tens"),
        (r"\b(galaxia|nebulosa|univers)\b", "galaxia", "La teva vault, en estrelles"),
        (r"\b(torna|mascota)\b", "mascota", "Torno"),
    ],
    "zh": [   # en chino no hay espacios ni \b
        (r"番茄", "pomodoro", "番茄钟开始：25 分钟"),
        (r"跳舞", "baila", "一起跳舞！"),
        (r"睡觉|晚安", "dormir", "晚安"),
        (r"醒醒|起床|醒来", "despertar", "我醒了"),
        (r"打开声音|开声音|有声音", "sonido", "声音已打开"),
        (r"静音|安静|别出声", "mudo", "声音已关闭"),
        (r"飞翔|飞", "vuela", "飞起来！"),
        (r"奔跑|跑步", "corre", "跑起来！"),
        (r"玩游戏|游戏|玩", "juegos", "来玩吧！"),
        (r"进度|怎么样了|在做什么", "claude", "这是 Claude 的进度"),
        (r"跳一下|跳", "anim:jump", "嘿！"),
        (r"转一圈|转圈|转身", "anim:turn", "转一圈"),
        (r"眨眼", "anim:wink", ";)"),
        (r"打喷嚏", "anim:sneeze", "阿嚏！"),
        (r"躲起来|藏起来", "anim:peekaboo", "躲猫猫！"),
        (r"庆祝|派对", "anim:celebrate", "耶！"),
        (r"走一走|散步", "anim:waddle", "散散步"),
        (r"终端", "terminal", "在这里"),
        (r"星系|宇宙|笔记库", "galaxia", "你的笔记库，变成星星"),
        (r"回来|宠物", "mascota", "回来啦"),
    ],
}

QUE_HORA = {"es": r"\bque hora es\b", "en": r"\bwhat time is it\b|\bwhat s the time\b",
            "ca": r"\bquina hora es\b", "zh": r"几点"}
CONV_NUEVA = {"es": r"\b(nueva conversacion|empieza de cero|olvida todo)\b",
              "en": r"\b(new conversation|start over|forget everything)\b",
              "ca": r"\b(nova conversa|comenca de zero|oblida ho tot)\b", "zh": r"新对话|重新开始"}
TIMER = {"es": r"\b(temporizador|timer|cuenta atras|alarma|avisame en|avisame dentro)\b",
         "en": r"\b(timer|countdown|alarm|remind me in)\b",
         "ca": r"\b(temporitzador|compte enrere|alarma|avisa m en|avisa m d aqui)\b",
         "zh": r"计时|定时|倒计时|闹钟|提醒我"}
CANCELAR = {"es": r"\b(cancela|quita|para|borra)\b", "en": r"\b(cancel|stop|remove)\b",
            "ca": r"\b(cancel la|treu|atura|esborra)\b", "zh": r"取消|停止|关掉"}


def frase_corta(t: str, idioma: str, max_palabras: int = 5) -> bool:
    """Las órdenes solo valen en frases cortas (en chino se cuentan caracteres)."""
    if idioma == "zh":
        return len(t.replace(" ", "")) <= max_palabras * 2
    return len(t.split()) <= max_palabras


# ------------------------------------------------------------------ números (temporizador)
NUMEROS = {
    "es": {"un": 1, "uno": 1, "una": 1, "dos": 2, "tres": 3, "cuatro": 4, "cinco": 5, "seis": 6, "siete": 7,
           "ocho": 8, "nueve": 9, "diez": 10, "once": 11, "doce": 12, "quince": 15, "veinte": 20,
           "veinticinco": 25, "treinta": 30, "cuarenta": 40, "cuarenta y cinco": 45, "cincuenta": 50,
           "sesenta": 60, "noventa": 90},
    "en": {"a": 1, "an": 1, "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7,
           "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12, "fifteen": 15, "twenty": 20,
           "twenty five": 25, "thirty": 30, "forty": 40, "forty five": 45, "fifty": 50, "sixty": 60, "ninety": 90},
    "ca": {"un": 1, "una": 1, "dos": 2, "dues": 2, "tres": 3, "quatre": 4, "cinc": 5, "sis": 6, "set": 7,
           "vuit": 8, "nou": 9, "deu": 10, "onze": 11, "dotze": 12, "quinze": 15, "vint": 20,
           "vint i cinc": 25, "trenta": 30, "quaranta": 40, "quaranta cinc": 45, "cinquanta": 50,
           "seixanta": 60, "noranta": 90},
}
UNIDADES = {
    "es": (("hora", 3600), ("minut", 60), ("segund", 1)),
    "en": (("hour", 3600), ("minute", 60), ("second", 1)),
    "ca": (("hora", 3600), ("hore", 3600), ("minut", 60), ("segon", 1)),
}
MEDIA_HORA = {"es": "media hora", "en": "half an hour", "ca": "mitja hora", "zh": "半小时"}


def numero_chino(s: str) -> int:
    """十五 -> 15, 二十 -> 20, 两 -> 2, 三十五 -> 35"""
    d = {"零": 0, "一": 1, "二": 2, "两": 2, "三": 3, "四": 4, "五": 5, "六": 6, "七": 7, "八": 8, "九": 9}
    if s.isdigit():
        return int(s)
    if "十" in s:
        a, _, b = s.partition("十")
        return (d.get(a, 1) if a else 1) * 10 + (d.get(b, 0) if b else 0)
    return d.get(s, 0)


def segundos_de(t: str, idioma: str):
    """'diez minutos' -> 600; 'half an hour' -> 1800; '一个小时十五分钟' -> 4500. None si no hay tiempo."""
    total, encontrado = 0, False
    if MEDIA_HORA[idioma] in t:
        total, encontrado = 1800, True
        t = t.replace(MEDIA_HORA[idioma], " ")
    if idioma == "zh":
        for n, unidad in re.findall(r"(\d+|[零一二两三四五六七八九十]+)\s*个?\s*(小时|钟头|分钟|分|秒钟|秒)", t):
            factor = 3600 if unidad in ("小时", "钟头") else 60 if unidad in ("分钟", "分") else 1
            total += numero_chino(n) * factor
            encontrado = True
        return total if encontrado else None
    palabras = "|".join(sorted(NUMEROS[idioma], key=len, reverse=True))
    for unidad, factor in UNIDADES[idioma]:
        m = re.search(r"(\d+|" + palabras + r")\s+" + unidad, t)
        if m:
            n = m.group(1)
            total += (int(n) if n.isdigit() else NUMEROS[idioma][n]) * factor
            encontrado = True
    return total if encontrado else None


def duracion_texto(s: int) -> str:
    if s % 3600 == 0:
        return f"{s // 3600} h"
    if s >= 60:
        return f"{s // 60} min" + (f" {s % 60} s" if s % 60 else "")
    return f"{s} s"


# ------------------------------------------------------------------ fecha
def fecha(idioma: str, ahora: datetime.datetime) -> str:
    wd, d, m = ahora.weekday(), ahora.day, ahora.month - 1
    if idioma == "en":
        return f"{['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'][wd]} {d} " \
               f"{['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][m]}"
    if idioma == "zh":
        return f"{m + 1}月{d}日 周{'一二三四五六日'[wd]}"
    if idioma == "ca":
        return f"{['dl.', 'dt.', 'dc.', 'dj.', 'dv.', 'ds.', 'dg.'][wd]} {d} " \
               f"{['gen.', 'febr.', 'març', 'abr.', 'maig', 'juny', 'jul.', 'ag.', 'set.', 'oct.', 'nov.', 'des.'][m]}"
    return f"{['lun', 'mar', 'mié', 'jue', 'vie', 'sáb', 'dom'][wd]} {d} " \
           f"{['ene', 'feb', 'mar', 'abr', 'may', 'jun', 'jul', 'ago', 'sep', 'oct', 'nov', 'dic'][m]}"


# Cómo escribir "Claude" / "Clawd" para que cada voz lo pronuncie bien (probado con Whisper:
# "Claude" tal cual salía como "Claudia", "plot", "各" o "la clota")
PRONUNCIA = {"es": "Clod", "en": "Clawed", "zh": "克劳德", "ca": "Clòd"}


def para_decir(texto: str, idioma: str) -> str:
    return re.sub(r"\b(Claude|Clawd)\b", PRONUNCIA.get(idioma, "Clod"), texto)


def idioma_de_texto(texto: str, por_defecto: str) -> str:
    """Para elegir la voz: si hay caracteres chinos, chino; si no, el que se diga."""
    return "zh" if re.search(r"[一-鿿]", texto) else por_defecto
