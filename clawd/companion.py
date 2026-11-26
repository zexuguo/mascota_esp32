# /// script
# requires-python = ">=3.11,<3.14"
# dependencies = ["pyserial", "psutil", "faster-whisper", "numpy"]
# ///
"""Companion de Clawd. Hace tres cosas a la vez:

1. Cada segundo manda a la placa los datos del PC (CPU, temperatura, batería,
   hora y qué está haciendo Claude Code) en una línea JSON.
2. Cuando mantienes pulsado y hablas, recibe tu voz, la pasa a texto con
   Whisper (en tu ordenador, sin internet) y...
3. ...si es una orden para Clawd ("baila", "pomodoro"...), se la manda;
   si no, se la pasa a Claude Code (`claude -p`) y le enseña a la placa
   cada paso y la respuesta.

Uso:   uv run companion.py           (puerto /dev/ttyACM0)
       uv run companion.py --mic     (además, imprime los niveles del micro)

La primera vez tarda: descarga Whisper (~500 MB).
"""
import datetime
import os
import signal
import json
import re
import shutil
import subprocess
import sys
import threading
import time
import unicodedata
import wave
from base64 import b64decode
from pathlib import Path

import numpy as np
import psutil
import serial

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
PUERTO = ARGS[0] if ARGS else "/dev/ttyACM0"
VER_MIC = "--mic" in sys.argv

ESTADO_CLAUDE = Path.home() / ".local/state/zen-claude/estado.json"   # lo escriben los hooks
CARPETA_ESTADO = Path.home() / ".local/state/clawd"
CARPETA_TRABAJO = Path.home()          # donde trabaja Claude Code por voz
MODELO_WHISPER = "small"               # "base" = más rápido, "medium" = entiende mejor
SEGUNDOS_PARA_CANCELAR = 3
VAULT = Path.home() / "obsidian"       # para la pantalla de la galaxia
MAX_ANILLOS = 12                       # la placa guarda como mucho 12 carpetas

DIAS = ["lun", "mar", "mie", "jue", "vie", "sab", "dom"]
MESES = ["ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"]
HAY_NVIDIA = shutil.which("nvidia-smi") is not None

# Permisos de Claude por voz: TODOS (lo pidió zexu el 2026-11-26).
# "bypassPermissions" = puede leer, editar, borrar y ejecutar cualquier cosa sin preguntar.
# Lo que queda de seguridad: los 3 s para cancelar en la placa (por si Whisper entiende
# otra cosa) y la instrucción de abajo de preguntar antes de lo irreversible.
# Para volver a limitarlo: "dontAsk" + "--allowedTools" con una lista.
MODO_PERMISOS = "bypassPermissions"
INSTRUCCIONES = (
    "Te habla zexu por voz a través de Clawd, una mascota con una pantalla "
    "diminuta de 32 columnas. Responde en español y MUY breve: 1-3 frases, "
    "sin markdown, sin tablas, sin listas largas, sin emojis. Lo que te dice "
    "viene de un reconocimiento de voz y puede tener errores: si algo no tiene "
    "sentido o es ambiguo, pregunta en vez de adivinar. Tienes todos los permisos: "
    "antes de algo destructivo o irreversible (borrar, sobrescribir, git push...) "
    "pregunta primero y espera a que te diga que sí."
)


def log(*a):
    print(f"{datetime.datetime.now():%H:%M:%S} ", *a, flush=True)


def ascii_pantalla(texto: str) -> str:
    """Prepara texto para la pantalla: la letra de la placa tiene tildes y ñ,
    pero no emojis ni símbolos raros; y fuera el markdown."""
    for a, b in {"“": '"', "”": '"', "‘": "'", "’": "'", "—": "-", "–": "-", "→": "->", "•": "·"}.items():
        texto = texto.replace(a, b)
    texto = re.sub(r"[*_`#]+", "", texto)
    permitidos = set("…·●└")
    texto = "".join(c for c in texto if c == "\n" or 32 <= ord(c) < 0x180 or c in permitidos)
    return re.sub(r"\n{3,}", "\n\n", texto).strip()


def solo_ascii(texto: str) -> str:
    texto = unicodedata.normalize("NFKD", texto)
    return "".join(c for c in texto if not unicodedata.combining(c))


def normalizar(texto: str) -> str:
    t = solo_ascii(ascii_pantalla(texto)).lower()
    return re.sub(r"[^a-z0-9 ]", " ", t)


# ---------------------------------------------------------------- temporizador por voz
NUMEROS = {"un": 1, "uno": 1, "una": 1, "dos": 2, "tres": 3, "cuatro": 4, "cinco": 5, "seis": 6,
           "siete": 7, "ocho": 8, "nueve": 9, "diez": 10, "once": 11, "doce": 12, "quince": 15,
           "veinte": 20, "veinticinco": 25, "treinta": 30, "cuarenta": 40, "cuarenta y cinco": 45,
           "cincuenta": 50, "noventa": 90}


def segundos_de(t: str):
    """'diez minutos' -> 600, 'media hora' -> 1800, '1 hora y 15 minutos' -> 4500. None si no hay tiempo."""
    if "media hora" in t:
        return 1800 + segundos_de(t.replace("media hora", "")) if re.search(r"\d|minut", t.replace("media hora", "")) else 1800
    total, encontrado = 0, False
    for unidad, factor in (("hora", 3600), ("minut", 60), ("segund", 1)):
        m = re.search(r"(\d+|" + "|".join(sorted(NUMEROS, key=len, reverse=True)) + r")\s+" + unidad, t)
        if m:
            n = m.group(1)
            total += (int(n) if n.isdigit() else NUMEROS[n]) * factor
            encontrado = True
    return total if encontrado else None


def duracion_texto(s: int) -> str:
    if s % 3600 == 0:
        return f"{s // 3600} h"
    if s >= 60:
        return f"{s // 60} min" + (f" {s % 60} s" if s % 60 else "")
    return f"{s} s"


# ---------------------------------------------------------------- datos del PC
def temp_cpu() -> float:
    temps = psutil.sensors_temperatures()
    for s in temps.get("coretemp", []):
        if s.label.startswith("Package"):
            return s.current
    for s in temps.get("k10temp", []):
        return s.current
    return 0.0


def gpu() -> tuple[float, float]:
    if not HAY_NVIDIA:
        return 0.0, 0.0
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu,temperature.gpu",
                              "--format=csv,noheader,nounits"],
                             capture_output=True, text=True, timeout=2).stdout
        uso, temp = out.strip().split(",")
        return float(uso), float(temp)
    except Exception:
        return 0.0, 0.0


def estado_claude() -> str:
    try:
        estado = json.loads(ESTADO_CLAUDE.read_text()).get("estado", "")
        antiguedad = time.time() - ESTADO_CLAUDE.stat().st_mtime
    except Exception:
        return ""
    if estado in ("trabajando", "esperando") and antiguedad > 15 * 60:
        return ""
    return estado


# ---------------------------------------------------------------- el companion
# ------------------------------------------------- vigilar a Claude Code
PROYECTOS_CLAUDE = Path.home() / ".claude/projects"   # aquí apunta cada sesión, paso a paso
TAPA = next(iter(Path("/proc/acpi/button/lid").glob("*/state")), None)


def tapa_cerrada() -> bool:
    try:
        return TAPA is not None and "closed" in TAPA.read_text()
    except Exception:
        return False


class Vigilante:
    """Lee el registro (.jsonl) de la sesión de Claude Code más reciente y saca
    el proyecto, cada herramienta que usa, la lista de tareas y su último mensaje."""

    def __init__(self):
        self.archivo: Path | None = None
        self.posicion = 0
        self.proyecto = ""
        self.tareas: list[dict] = []       # {"id", "texto", "estado"}
        self.ultimo_texto = ""
        self.nuevas_acciones: list[str] = []
        self.cambio_tareas = False

    def sesion_activa(self) -> Path | None:
        try:
            todos = list(PROYECTOS_CLAUDE.glob("*/*.jsonl"))
        except Exception:
            return None
        if not todos:
            return None
        f = max(todos, key=lambda p: p.stat().st_mtime)
        return f if time.time() - f.stat().st_mtime < 20 * 60 else None

    def leer(self):
        f = self.sesion_activa()
        if f is None:
            return
        if f != self.archivo:                 # sesión nueva: empezar de cero
            self.archivo, self.posicion = f, 0
            self.tareas, self.ultimo_texto, self.proyecto = [], "", ""
            self.cambio_tareas = True
        with open(f, "rb") as fh:
            fh.seek(self.posicion)
            datos = fh.read()
        # solo líneas completas
        corte = datos.rfind(b"\n") + 1
        self.posicion += corte
        primera_vez = self.posicion == corte   # al abrir: no mandar 500 acciones viejas
        acciones = []
        for linea in datos[:corte].splitlines():
            try:
                d = json.loads(linea)
            except Exception:
                continue
            if d.get("cwd"):
                self.proyecto = Path(d["cwd"]).name or "~"
            if d.get("type") != "assistant" or d.get("isSidechain"):
                continue
            for c in d.get("message", {}).get("content", []):
                if c.get("type") == "text" and c.get("text", "").strip():
                    self.ultimo_texto = c["text"].strip()
                if c.get("type") != "tool_use":
                    continue
                self.apuntar_tarea(c.get("name", ""), c.get("input") or {})
                acciones.append(Companion.describir(c))
        self.nuevas_acciones += acciones[-6:] if primera_vez else acciones

    def apuntar_tarea(self, nombre: str, e: dict):
        if nombre == "TaskCreate":
            self.tareas.append({"id": str(len(self.tareas) + 1), "texto": e.get("subject", ""), "estado": "pending"})
        elif nombre == "TaskUpdate":
            for t in self.tareas:
                if t["id"] == str(e.get("taskId")):
                    if e.get("status") == "deleted":
                        self.tareas.remove(t)
                    elif e.get("status"):
                        t["estado"] = e["status"]
                    if e.get("subject"):
                        t["texto"] = e["subject"]
                    break
        elif nombre == "TodoWrite":
            self.tareas = [{"id": str(i), "texto": t.get("content", ""), "estado": t.get("status", "pending")}
                           for i, t in enumerate(e.get("todos", []))]
        else:
            return
        self.cambio_tareas = True

    def tareas_para_placa(self) -> list:
        cod = {"pending": 0, "in_progress": 1, "completed": 2}
        return [[cod.get(t["estado"], 0), ascii_pantalla(t["texto"])[:40]] for t in self.tareas[:12]]


class Companion:
    def __init__(self):
        self.s: serial.Serial | None = None
        self.lock = threading.Lock()
        self.audio = bytearray()
        self.ocupado = False                  # Claude trabajando por voz
        self.cancelar = threading.Event()
        self.proc: subprocess.Popen | None = None
        self.whisper = None
        self.captura = bytearray()
        self.captura_pantalla = 0
        CARPETA_ESTADO.mkdir(parents=True, exist_ok=True)
        self.f_sesion = CARPETA_ESTADO / "sesion"
        self.sesion = self.f_sesion.read_text().strip() if self.f_sesion.exists() else None
        threading.Thread(target=self.cargar_whisper, daemon=True).start()
        self.vigilante = Vigilante()
        self.inhibidor: subprocess.Popen | None = None   # "no te suspendas"
        self.aviso_pendiente = False         # Claude acabó y aún no lo has visto
        self.acabo_en = 0.0
        self.estado_antes = ""

    def cargar_whisper(self):
        try:
            from faster_whisper import WhisperModel
            log(f"Cargando Whisper '{MODELO_WHISPER}'...")
            self.whisper = WhisperModel(MODELO_WHISPER, device="cpu", compute_type="int8")
            log("Whisper listo: ya puedes hablarle")
        except Exception as e:
            log("No se pudo cargar Whisper:", e)

    def enviar(self, d: dict):
        s = self.s
        if not s:
            return
        try:
            with self.lock:
                s.write((json.dumps(d) + "\n").encode())
            self.atascada = False
        except serial.SerialTimeoutException:
            if not getattr(self, "atascada", False):
                log("La placa no lee lo que le mando: se ha colgado? (pulsa RESET)")
            self.atascada = True
        except Exception:
            pass

    # --- la galaxia del vault (como el plugin Nebula): notas por carpeta ---
    @staticmethod
    def galaxia() -> dict:
        carpetas = []
        if VAULT.is_dir():
            for c in sorted(VAULT.iterdir()):
                if c.is_dir() and not c.name.startswith("."):
                    n = sum(1 for _ in c.rglob("*.md"))
                    if n:
                        carpetas.append(n)
        return {"galaxia": carpetas[:MAX_ANILLOS], "notas": sum(carpetas)}

    # --- datos cada segundo ---
    def telemetria(self):
        psutil.cpu_percent()
        vuelta = 0
        while True:
            if vuelta % 30 == 0:              # la galaxia, cada 30 s (y al empezar)
                try:
                    self.enviar(self.galaxia())
                except Exception as e:
                    log("Galaxia:", e)
            vuelta += 1
            ahora = datetime.datetime.now()
            bat = psutil.sensors_battery()
            _, temp_gpu = gpu()
            self.enviar({
                "cpu": round(psutil.cpu_percent()),
                "ct": round(max(temp_cpu(), temp_gpu)),
                "ram": round(psutil.virtual_memory().percent),
                "bat": round(bat.percent) if bat else -1,
                "carga": int(bat.power_plugged) if bat else 1,
                "h": ahora.hour, "m": ahora.minute, "s": ahora.second,
                "dia": f"{DIAS[ahora.weekday()]} {ahora.day} {MESES[ahora.month - 1]}",
                "claude": estado_claude(),
                "voz": int(self.ocupado),
                "tapa": int(tapa_cerrada()),
                "insomne": int(self.inhibidor is not None),
            })
            time.sleep(1)

    # --- lo que llega de la placa ---
    def al_recibir(self, linea: bytes):
        try:
            d = json.loads(linea)
        except Exception:
            # no es JSON: arranque de la placa o un error suyo. Guardarlo.
            texto = linea.decode(errors="replace").rstrip()
            if texto:
                log("placa>", texto)
                with open(CARPETA_ESTADO / "placa.log", "a") as f:
                    f.write(f"{datetime.datetime.now():%F %T} {texto}\n")
            return
        if "cy" in d:                                  # un trozo de captura de pantalla
            y = d["cy"] * 480
            trozo = b64decode(d["cd"])
            self.captura[y:y + len(trozo)] = trozo
            return
        if d.get("cap") == "ini":
            self.captura = bytearray(d["w"] * d["h"] * 2)
            self.captura_pantalla = d.get("p", 0)
            return
        if d.get("cap") == "fin":
            self.guardar_captura()
            return
        if "a" in d:                                   # un trozo de voz
            self.audio += b64decode(d["a"])
            return
        ev = d.get("ev")
        if ev == "mic":
            if VER_MIC:
                log(f"micro  suelo={d['suelo']:5}  suave={d['suave']:5}  pico={d['pico']:6}  "
                    f"{'BAILANDO' if d.get('baila') else ''}")
        elif ev == "voz_ini":
            self.audio = bytearray()
            log("Escuchando...")
        elif ev == "voz_fin":
            audio = bytes(self.audio)
            threading.Thread(target=self.procesar_voz, args=(audio,), daemon=True).start()
        elif ev == "visto":                         # has tocado el aviso de "Claude ha acabado"
            self.aviso_pendiente = False
            log("Aviso visto en la placa")
        elif ev == "voz_cancelar":                  # pulsado sin querer: tirar el audio
            self.audio = bytearray()
        elif ev == "cancelar":
            self.cancelar.set()
            if self.proc and self.proc.poll() is None:
                self.proc.terminate()
            log("Cancelado desde la placa")
        elif ev == "arranque":
            log(f"La placa ha arrancado (motivo: {d.get('motivo')}, ultimo paso: {d.get('paso')})")
        elif ev:
            log("Clawd:", ev)

    # --- capturas de pantalla de la placa (PNG) ---
    def guardar_captura(self):
        import struct
        import zlib
        w, h = 240, 320
        px = np.frombuffer(bytes(self.captura), dtype=">u2").reshape(h, w)   # RGB565, byte alto primero
        rgb = np.stack([((px >> 11) & 31) * 255 // 31, ((px >> 5) & 63) * 255 // 63, (px & 31) * 255 // 31],
                       axis=-1).astype(np.uint8)
        crudo = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))
        def trozo(tipo, datos):
            return struct.pack(">I", len(datos)) + tipo + datos + struct.pack(">I", zlib.crc32(tipo + datos))
        png = (b"\x89PNG\r\n\x1a\n" + trozo(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
               trozo(b"IDAT", zlib.compress(crudo, 9)) + trozo(b"IEND", b""))
        carpeta = CARPETA_ESTADO / "capturas"
        carpeta.mkdir(exist_ok=True)
        nombre = carpeta / f"{datetime.datetime.now():%H%M%S}-p{self.captura_pantalla}.png"
        nombre.write_bytes(png)
        (carpeta / "ultima.png").write_bytes(png)
        log("Captura guardada:", nombre)

    # --- órdenes desde un archivo (para probar y revisar sin tocar la placa) ---
    # ~/.local/state/clawd/orden.json = [{"cmd": "pantalla:1"}, {"esperar": 1}, {"cmd": "captura"}, {"enviar": {...}}]
    def ejecutar_ordenes(self):
        f = CARPETA_ESTADO / "orden.json"
        if not f.exists():
            return
        try:
            pasos = json.loads(f.read_text())
        except Exception as e:
            log("orden.json no vale:", e)
            pasos = []
        f.unlink(missing_ok=True)
        for p in pasos:
            if "cmd" in p:
                self.enviar({"cmd": p["cmd"]})
            if "enviar" in p:
                self.enviar(p["enviar"])
            if "esperar" in p:
                time.sleep(float(p["esperar"]))

    # --- voz -> texto ---
    def transcribir(self, audio: bytes) -> str:
        muestras = np.frombuffer(audio, dtype=np.int16).astype(np.float32) / 32768
        muestras -= muestras.mean()
        pico = np.abs(muestras).max()
        if pico > 0:
            muestras *= 0.9 / pico                     # el micro es flojito: subir volumen
        with wave.open(str(CARPETA_ESTADO / "ultima_voz.wav"), "wb") as w:   # para depurar
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
            w.writeframes((muestras * 32767).astype(np.int16).tobytes())
        segs, _ = self.whisper.transcribe(
            muestras, language="es", vad_filter=True,
            initial_prompt="Clawd, Claude, pomodoro, archivo, carpeta, proyecto, commit.")
        return " ".join(s.text for s in segs).strip()

    def procesar_voz(self, audio: bytes):
        segundos = len(audio) / 32000
        log(f"Voz recibida: {segundos:.1f} s")
        if segundos < 0.5:
            self.enviar({"err": "Muy corto: manten pulsado mientras hablas"})
            return
        if not self.whisper:
            self.enviar({"err": "Whisper aun se esta cargando, espera un poco"})
            return
        t = time.time()
        texto = self.transcribir(audio)
        log(f"Entendido ({time.time() - t:.1f} s): {texto!r}")
        if not texto:
            self.enviar({"err": "No te he entendido"})
            return
        if self.orden_local(texto):
            return
        if self.ocupado:
            self.enviar({"oido": ascii_pantalla(texto), "cuenta": 0})
            self.enviar({"err": "Claude sigue con lo anterior"})
            return
        self.enviar({"oido": ascii_pantalla(texto), "cuenta": SEGUNDOS_PARA_CANCELAR})
        self.cancelar.clear()
        self.ocupado = True                        # para que la placa sepa que puede cancelar
        if self.cancelar.wait(SEGUNDOS_PARA_CANCELAR):
            self.ocupado = False
            return
        try:
            self.preguntar_claude(texto)
        finally:
            self.ocupado = False
            self.proc = None

    # --- órdenes para Clawd (solo si la frase es corta) ---
    def orden_local(self, texto: str) -> bool:
        t = normalizar(texto)
        if re.search(r"\b(temporizador|timer|cuenta atras|alarma|avisame en|avisame dentro)\b", t) and len(t.split()) <= 10:
            if re.search(r"\b(cancela|quita|para|borra)\b", t):
                segundos = 0
            else:
                segundos = segundos_de(t)
            if segundos is not None:
                self.enviar({"oido": ascii_pantalla(texto), "cuenta": 0, "mascota": 1})
                self.enviar({"cmd": f"temporizador:{segundos}"})
                self.enviar({"local": "Temporizador cancelado" if segundos == 0 else f"Temporizador de {duracion_texto(segundos)}"})
                log("Temporizador:", segundos, "s")
                return True
        if len(t.split()) > 5:
            return False
        ordenes = [
            (r"\bpomodoro\b", "pomodoro", "Pomodoro de 25 min en marcha"),
            (r"\bbail", "baila", "A bailar!"),
            (r"\b(duermete|a dormir|buenas noches)\b", "dormir", "Buenas noches"),
            (r"\b(despierta|despiertate)\b", "despertar", "Ya estoy despierto"),
            (r"\b(silencio|callate)\b", "mudo", "Sonido apagado"),
            (r"\b(activa el sonido|pon el sonido|con sonido)\b", "sonido", "Sonido encendido"),
            (r"\b(a volar|juega a volar|vuela)\b", "vuela", "A volar!"),
            (r"\b(a correr|juega a correr|corre)\b", "corre", "A correr!"),
            (r"\b(juega|jugar|juegos|vamos a jugar)\b", "juegos", "A jugar!"),
            (r"\b(como va|progreso|que hace claude|como vas)\b", "claude", "Asi va Claude"),
            # las animaciones de Claude Code
            (r"\b(salta|un salto)\b", "anim:jump", "Hop!"),
            (r"\b(gira|da una vuelta|date la vuelta|vuelta)\b", "anim:turn", "Vueltecita"),
            (r"\b(guina|guino|un guino)\b", "anim:wink", ";)"),
            (r"\b(estornuda|achis)\b", "anim:sneeze", "Achis!"),
            (r"\b(escondete|cucu|esconde)\b", "anim:peekaboo", "Cucu!"),
            (r"\b(celebra|celebralo|fiesta)\b", "anim:celebrate", "Yuju!"),
            (r"\b(mira|mira alrededor)\b", "anim:look", "Mirando..."),
            (r"\b(asomate|asoma)\b", "anim:peek", "Hola?"),
            (r"\b(cae|caete|tirate)\b", "anim:drop", "Plof!"),
            (r"\b(moneda|haz la moneda)\b", "anim:coin-hop", "Tachan!"),
            (r"\b(anda|camina|pasea)\b", "anim:waddle", "Paseito"),
            (r"\b(zapatea|baile de pies)\b", "anim:tap", "Tap tap"),
            (r"\b(abre la terminal|terminal)\b", "terminal", "Aqui la tienes"),
            (r"\b(galaxia|nebula|universo|constelacion)\b", "galaxia", "Tu vault, en estrellas"),
            (r"\b(vuelve|mascota)\b", "mascota", "Vuelvo"),
        ]
        for patron, cmd, respuesta in ordenes:
            if re.search(patron, t):
                self.enviar({"oido": ascii_pantalla(texto), "cuenta": 0, "mascota": 1})
                self.enviar({"cmd": cmd})
                self.enviar({"local": respuesta})
                log("Orden para Clawd:", cmd)
                return True
        if re.search(r"\bque hora es\b", t):
            self.enviar({"oido": ascii_pantalla(texto), "cuenta": 0})
            self.enviar({"local": f"Son las {datetime.datetime.now():%H:%M}"})
            return True
        if re.search(r"\b(nueva conversacion|empieza de cero|olvida todo)\b", t):
            self.sesion = None
            self.f_sesion.unlink(missing_ok=True)
            self.enviar({"oido": ascii_pantalla(texto), "cuenta": 0})
            self.enviar({"local": "Conversacion nueva con Claude"})
            return True
        return False

    # --- Claude Code ---
    @staticmethod
    def describir(c: dict) -> str:
        nombre, e = c.get("name", ""), c.get("input") or {}
        verbos = {"Read": "Leo", "Edit": "Edito", "Write": "Escribo", "NotebookEdit": "Edito"}
        if nombre in verbos:
            return f"{verbos[nombre]} {Path(e.get('file_path', '')).name}"
        if nombre == "Bash":
            return "$ " + e.get("command", "")[:80]
        if nombre in ("Grep", "Glob"):
            return f"Busco {e.get('pattern', '')}"
        if nombre == "WebSearch":
            return "Busco en internet: " + e.get("query", "")
        if nombre == "WebFetch":
            return "Leo una web"
        if nombre == "TodoWrite":
            return "Organizo las tareas"
        return nombre

    def preguntar_claude(self, texto: str):
        cmd = ["claude", "-p", texto, "--output-format", "stream-json", "--verbose",
               "--permission-mode", MODO_PERMISOS, "--append-system-prompt", INSTRUCCIONES]
        if self.sesion:
            cmd += ["--resume", self.sesion]
        log("-> Claude:", texto)
        self.proc = subprocess.Popen(cmd, cwd=CARPETA_TRABAJO, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True)
        resultado = None
        for linea in self.proc.stdout:
            try:
                ev = json.loads(linea)
            except Exception:
                continue
            if ev.get("session_id"):
                self.sesion = ev["session_id"]
            if ev.get("type") == "assistant":
                for c in ev["message"].get("content", []):
                    if c.get("type") == "tool_use":
                        paso = self.describir(c)
                        log("   ", paso)
                        self.enviar({"paso": ascii_pantalla(paso)})
            elif ev.get("type") == "result":
                resultado = ev
        self.proc.wait()
        if self.sesion:
            self.f_sesion.write_text(self.sesion)
        if self.cancelar.is_set():
            return
        if not resultado:
            self.enviar({"err": "Claude no ha contestado"})
            return
        negados = resultado.get("permission_denials") or []
        if negados:
            n = negados[0]
            self.enviar({"err": "Sin permiso: " + ascii_pantalla(self.describir(
                {"name": n.get("tool_name"), "input": n.get("tool_input")}))})
        if resultado.get("subtype") != "success":
            self.enviar({"err": "Claude ha tenido un problema"})
        respuesta = ascii_pantalla(resultado.get("result") or "")[:1500]
        log("<- Claude:", respuesta)
        if respuesta:
            self.enviar({"resp": respuesta})

    # --- no dormirse mientras Claude trabaja (aunque cierres la tapa) ---
    def mantener_despierto(self, si: bool):
        if si and self.inhibidor is None:
            self.inhibidor = subprocess.Popen(
                ["systemd-inhibit", "--what=sleep:idle:handle-lid-switch", "--who=Clawd",
                 "--why=Claude Code esta trabajando", "--mode=block",
                 # en vez de "sleep infinity": vivir solo mientras viva el companion.
                 # Así, si el companion se cae o lo matan, el portátil vuelve a poder suspenderse.
                 "sh", "-c", f"while kill -0 {os.getpid()} 2>/dev/null; do sleep 3; done"])
            log("Claude trabaja: el portatil NO se suspendera aunque cierres la tapa")
        elif not si and self.inhibidor is not None:
            self.inhibidor.terminate()
            self.inhibidor = None
            log("Ya puede suspenderse" + (" (la tapa esta cerrada: se suspende ya)" if tapa_cerrada() else ""))

    def vigilar(self):
        while True:
            try:
                estado = estado_claude()
                self.ejecutar_ordenes()
                v = self.vigilante
                v.leer()
                if v.proyecto:
                    self.enviar({"proyecto": ascii_pantalla(v.proyecto)[:28]})
                for a in v.nuevas_acciones:
                    self.enviar({"act": ascii_pantalla(a)[:60]})
                v.nuevas_acciones = []
                if v.cambio_tareas:
                    self.enviar({"tareas": v.tareas_para_placa()})
                    v.cambio_tareas = False
                # acaba de terminar: mandar su último mensaje y dejar el aviso pendiente
                if estado == "terminado" and self.estado_antes in ("trabajando", "esperando"):
                    self.aviso_pendiente = True
                    self.acabo_en = time.time()
                    self.enviar({"resumen": ascii_pantalla(v.ultimo_texto)[:400]})
                    log("Claude ha terminado:", v.proyecto)
                if estado == "trabajando":
                    self.aviso_pendiente = False
                self.estado_antes = estado
                # despierto mientras trabaja o espera; y si acabó con la tapa cerrada,
                # 30 min más para que veas el aviso (si no, la placa se apagaría con el PC)
                aviso_reciente = self.aviso_pendiente and time.time() - self.acabo_en < 30 * 60
                self.mantener_despierto(estado in ("trabajando", "esperando") or
                                        (aviso_reciente and tapa_cerrada()))
            except Exception as e:
                log("Vigilante:", e)
            time.sleep(1)

    # --- bucle principal ---
    def correr(self):
        log(f"Companion de Clawd -> {PUERTO}  (Ctrl+C para salir)")
        threading.Thread(target=self.telemetria, daemon=True).start()
        threading.Thread(target=self.vigilar, daemon=True).start()
        avisado = False
        while True:
            try:
                with serial.Serial(PUERTO, 115200, timeout=0.1, write_timeout=2) as s:
                    self.s = s
                    log("Conectado a la placa")
                    avisado = False
                    resto = b""
                    while True:
                        resto += s.readline()         # puede llegar media línea: juntar
                        if resto.endswith(b"\n"):
                            self.al_recibir(resto)
                            resto = b""
            except (serial.SerialException, OSError):
                self.s = None
                if not avisado:
                    log("Esperando a la placa...")
                    avisado = True
                time.sleep(2)


if __name__ == "__main__":
    # systemctl stop manda SIGTERM: tratarlo como Ctrl+C para limpiar
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    c = Companion()
    try:
        c.correr()
    except KeyboardInterrupt:
        print("\nAdios!")
    finally:
        c.mantener_despierto(False)
