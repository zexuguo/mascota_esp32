# /// script
# requires-python = ">=3.11,<3.14"
# dependencies = ["pyserial", "psutil", "faster-whisper", "numpy", "piper-tts"]
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
import io
import os
import queue
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

import idiomas_pc as I        # español, inglés, chino y catalán (idiomas_pc.py, al lado)
IDIOMA_UI = {"actual": "es"}  # el que se ha elegido en la placa (Ajustes)

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

HAY_NVIDIA = shutil.which("nvidia-smi") is not None

# Permisos de Claude por voz: TODOS (lo pidió zexu el 2026-11-26).
# "bypassPermissions" = puede leer, editar, borrar y ejecutar cualquier cosa sin preguntar.
# Lo que queda de seguridad: los 3 s para cancelar en la placa (por si Whisper entiende
# otra cosa) y la instrucción de abajo de preguntar antes de lo irreversible.
# Para volver a limitarlo: "dontAsk" + "--allowedTools" con una lista.
MODO_PERMISOS = "bypassPermissions"
INSTRUCCIONES = (
    "Te habla zexu por voz a través de Clawd, una mascota con una pantalla "
    "diminuta de 32 columnas. Responde en el mismo idioma en que te hable (español, inglés, "
    "chino simplificado o catalán). MUY breve: 1-3 frases, "
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
    def vale(c: str) -> bool:
        o = ord(c)
        return (c == "\n" or 32 <= o < 0x180 or c in permitidos
                or 0x3000 <= o < 0x3040      # signos chinos: 。，、「」
                or 0x4E00 <= o < 0xA000      # caracteres chinos
                or 0xFF00 <= o < 0xFFF0)     # signos de ancho completo: ！？：
    texto = "".join(c for c in texto if vale(c))
    return re.sub(r"\n{3,}", "\n\n", texto).strip()


def solo_ascii(texto: str) -> str:
    texto = unicodedata.normalize("NFKD", texto)
    return "".join(c for c in texto if not unicodedata.combining(c))


def normalizar(texto: str) -> str:
    """minúsculas, sin tildes ni signos; el chino se queda tal cual"""
    t = solo_ascii(ascii_pantalla(texto)).lower()
    return re.sub(r"[^a-z0-9 \u4e00-\u9fff]", " ", t)


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
        self.idioma = "es"                    # el de la última frase (es o zh)
        self.captura = bytearray()
        self.permisos_enviados: set[str] = set()
        self.captura_pantalla = 0
        CARPETA_ESTADO.mkdir(parents=True, exist_ok=True)
        self.f_sesion = CARPETA_ESTADO / "sesion"
        self.sesion = self.f_sesion.read_text().strip() if self.f_sesion.exists() else None
        threading.Thread(target=self.cargar_whisper, daemon=True).start()
        self.tts = None
        self.hablar_on = True               # la placa lo cambia desde la cortina ("Habla")
        self.cola_habla: queue.Queue[str] = queue.Queue()
        self.callar = threading.Event()
        threading.Thread(target=self.hablador, daemon=True).start()
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

    # --- la voz de Clawd: Piper genera el audio y se manda a la placa en tiempo real ---
    def decir(self, texto: str, idioma: str | None = None):
        """Decirlo en voz alta. Idioma: el que se diga, o chino si hay caracteres chinos,
        o el de la última frase que le has dicho, o el de la placa."""
        if self.hablar_on and texto.strip():
            idioma = I.idioma_de_texto(texto, idioma or self.idioma or IDIOMA_UI["actual"])
            self.cola_habla.put((texto, idioma if idioma in I.IDIOMAS else "es"))

    def voz(self, idioma: str):
        """La voz de Piper de ese idioma (se carga la primera vez que hace falta)."""
        if idioma not in self.voces:
            from piper import PiperVoice
            f = I.CARPETA_VOCES / f"{I.VOCES[idioma]}.onnx"
            self.voces[idioma] = PiperVoice.load(str(f)) if f.exists() else None
            log(f"Voz {idioma}:", "lista" if self.voces[idioma] else f"falta {f.name}")
        return self.voces[idioma] or self.voces.get("es")

    def hablador(self):
        self.voces = {}
        try:
            self.voz("es")
        except Exception as e:
            log("Sin voz (Piper):", e)
            return
        while True:
            texto, idioma = self.cola_habla.get()
            self.callar.clear()
            # que pronuncie bien: "Clawd" lo leería como "clavos"; fuera rutas y símbolos
            texto = I.para_decir(texto, idioma)    # "Claude" -> como se pronuncia en ese idioma
            texto = re.sub(r"[`*_#>|]", " ", texto)
            try:
                tts = self.voz(idioma)
                buf = io.BytesIO()
                with wave.open(buf, "wb") as w:
                    tts.synthesize_wav(texto[:600], w)
                buf.seek(0)
                with wave.open(buf) as w:
                    sr = w.getframerate()
                    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32)
                # 22050 Hz -> 16000 Hz (lo que usa la placa) y volumen normalizado
                x = np.interp(np.arange(0, len(x), sr / 16000), np.arange(len(x)), x)
                pico = np.abs(x).max() or 1
                pcm = (x * (0.7 * 32767 / pico)).astype("<i2").tobytes()
            except Exception as e:
                log("No he podido decirlo:", e)
                continue
            # mandar en trozos de 30 ms, a ritmo de tiempo real (con 0,3 s de ventaja)
            trozo = 480 * 2
            inicio = time.time()
            for i in range(0, len(pcm), trozo):
                if self.callar.is_set():
                    self.enviar({"callar": 1})
                    break
                espera = inicio + (i / trozo) * 0.03 - 0.3 - time.time()
                if espera > 0:
                    time.sleep(espera)
                from base64 import b64encode
                self.enviar({"v": b64encode(pcm[i:i + trozo]).decode()})

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
            # "estoy vivo": el hook de permisos solo usa la placa si el companion está en marcha
            if self.s:
                (CARPETA_ESTADO / "vivo").touch()
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
                "dia": I.fecha(IDIOMA_UI["actual"], ahora),
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
            self.callar.set()                         # si estaba hablando, que se calle
            self.audio = bytearray()
            log("Escuchando...")
        elif ev == "voz_fin":
            audio = bytes(self.audio)
            threading.Thread(target=self.procesar_voz, args=(audio,), daemon=True).start()
        elif ev == "voz_auto_ini":                    # la placa ha notado voz: ¿es "Oye Clawd"?
            self.audio = bytearray()
        elif ev == "voz_auto_fin":
            audio = bytes(self.audio)
            threading.Thread(target=self.procesar_llamada, args=(audio,), daemon=True).start()
        elif ev and ev.startswith("idioma:"):        # Ajustes -> Idioma en la placa
            IDIOMA_UI["actual"] = ev[7:] if ev[7:] in I.IDIOMAS else "es"
            log("Idioma de la placa:", I.NOMBRE[IDIOMA_UI["actual"]])
        elif ev in ("habla:1", "habla:0"):          # interruptor "Habla" de la cortina
            self.hablar_on = ev == "habla:1"
            if not self.hablar_on:
                self.callar.set()
        elif ev == "permiso":                       # has contestado un permiso en la placa
            r = CARPETA_ESTADO / "permisos" / f"{d.get('id')}.respuesta"
            r.write_text(json.dumps({"ok": bool(d.get("ok"))}))
            log("Permiso", "CONCEDIDO" if d.get("ok") else "DENEGADO", "desde la placa")
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
            if "habla" in d:
                self.hablar_on = bool(d["habla"])
            if d.get("idioma") in I.IDIOMAS:
                IDIOMA_UI["actual"] = d["idioma"]
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
            if "decir" in p:
                self.decir(p["decir"])

    # --- voz -> texto ---
    def transcribir(self, audio: bytes, guardar: bool = True) -> str:
        muestras = np.frombuffer(audio, dtype=np.int16).astype(np.float32) / 32768
        muestras -= muestras.mean()
        pico = np.abs(muestras).max()
        if pico > 0:
            muestras *= 0.9 / pico                     # el micro es flojito: subir volumen
        if guardar:   # para depurar (se sobrescribe); lo de "Oye Clawd" no se guarda nunca
            with wave.open(str(CARPETA_ESTADO / "ultima_voz.wav"), "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
                w.writeframes((muestras * 32767).astype(np.int16).tobytes())
        # Idioma: lo detecta Whisper. Valen español, inglés, chino y catalán; si cree que es
        # otro (pasa con frases muy cortas), se repite en el idioma elegido en la placa.
        ui = IDIOMA_UI["actual"]
        opciones = dict(vad_filter=True, initial_prompt=I.PISTA[ui])
        segs, info = self.whisper.transcribe(muestras, language=None, **opciones)
        if info.language not in I.IDIOMAS:
            segs, info = self.whisper.transcribe(muestras, language=ui, **opciones)
        self.idioma = info.language
        if info.language != ui:
            log("Idioma detectado:", I.NOMBRE[info.language])
        if info.language == "zh":
            return "".join(s.text for s in segs).strip()   # el chino no separa con espacios
        return " ".join(s.text for s in segs).strip()

    # --- "Oye Clawd": solo se hace caso si empieza por la llamada; si no, se tira ---

    def procesar_llamada(self, audio: bytes):
        segundos = len(audio) / 32000
        if not self.whisper or segundos < 0.5 or self.ocupado:
            return
        texto = self.transcribir(audio, guardar=False)
        llano = solo_ascii(texto)
        m = I.LLAMADA.match(llano)
        if not m:
            return                                     # no era para Clawd: no se guarda nada
        resto = llano[m.end():].strip()
        log(f"Oye Clawd ({segundos:.1f} s): {texto!r}")
        if len(resto) < 3:
            self.enviar({"cmd": "escuchar"})          # solo te ha llamado: ahora escucha lo que digas
        else:
            self.procesar_texto(resto)

    def procesar_voz(self, audio: bytes):
        segundos = len(audio) / 32000
        log(f"Voz recibida: {segundos:.1f} s")
        if segundos < 0.5:
            self.enviar({"err": I.msg("muy_corto", IDIOMA_UI["actual"])})
            return
        if not self.whisper:
            self.enviar({"err": I.msg("cargando", IDIOMA_UI["actual"])})
            return
        t = time.time()
        texto = self.transcribir(audio)
        log(f"Entendido ({time.time() - t:.1f} s): {texto!r}")
        if not texto:
            self.enviar({"err": I.msg("no_entendido", self.idioma or IDIOMA_UI["actual"])})
            return
        self.procesar_texto(texto)

    def procesar_texto(self, texto: str):
        if self.orden_local(texto):
            return
        if self.ocupado:
            self.enviar({"oido": ascii_pantalla(texto), "cuenta": 0})
            self.enviar({"err": I.msg("sigue", self.idioma or IDIOMA_UI["actual"])})
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
        idioma = self.idioma if self.idioma in I.IDIOMAS else IDIOMA_UI["actual"]
        t = normalizar(texto)
        oido = ascii_pantalla(texto)
        # temporizador ("pon un temporizador de diez minutos", "set a timer for five minutes", "定时十分钟")
        if re.search(I.TIMER[idioma], t) and I.frase_corta(t, idioma, 10):
            segundos = 0 if re.search(I.CANCELAR[idioma], t) else I.segundos_de(t, idioma)
            if segundos is not None:
                respuesta = I.msg("timer_cancel", idioma) if segundos == 0 else \
                            I.msg("timer_de", idioma, d=I.duracion_texto(segundos))
                self.enviar({"oido": oido, "cuenta": 0, "mascota": 1})
                self.enviar({"cmd": f"temporizador:{segundos}"})
                self.enviar({"local": respuesta})
                self.decir(respuesta, idioma)
                log("Temporizador:", segundos, "s")
                return True
        if not I.frase_corta(t, idioma):
            return False
        for patron, cmd, respuesta in I.ORDENES[idioma]:
            if re.search(patron, t):
                self.enviar({"oido": oido, "cuenta": 0, "mascota": 1})
                self.enviar({"cmd": cmd})
                self.enviar({"local": respuesta})
                self.decir(respuesta, idioma)
                log("Orden para Clawd:", cmd)
                return True
        if re.search(I.QUE_HORA[idioma], t):
            respuesta = I.msg("son_las", idioma, h=f"{datetime.datetime.now():%H:%M}")
            self.enviar({"oido": oido, "cuenta": 0})
            self.enviar({"local": respuesta})
            self.decir(respuesta, idioma)
            return True
        if re.search(I.CONV_NUEVA[idioma], t):
            self.sesion = None
            self.f_sesion.unlink(missing_ok=True)
            self.enviar({"oido": oido, "cuenta": 0})
            self.enviar({"local": I.msg("conv_nueva", idioma)})
            return True
        return False

    # --- Claude Code ---
    @staticmethod
    def describir(c: dict) -> str:
        ui = IDIOMA_UI["actual"]
        nombre, e = c.get("name", ""), c.get("input") or {}
        verbos = {"Read": "leo", "Edit": "edito", "Write": "escribo", "NotebookEdit": "edito"}
        if nombre in verbos:
            return f"{I.msg(verbos[nombre], ui)} {Path(e.get('file_path', '')).name}"
        if nombre == "Bash":
            return "$ " + e.get("command", "")[:80]
        if nombre in ("Grep", "Glob"):
            return f"{I.msg('busco', ui)} {e.get('pattern', '')}"
        if nombre == "WebSearch":
            return I.msg("internet", ui) + e.get("query", "")
        if nombre == "WebFetch":
            return I.msg("web", ui)
        if nombre == "TodoWrite":
            return I.msg("tareas", ui)
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
            self.enviar({"err": I.msg("claude_no", self.idioma or IDIOMA_UI["actual"])})
            return
        negados = resultado.get("permission_denials") or []
        if negados:
            n = negados[0]
            self.enviar({"err": I.msg("sin_permiso", self.idioma or IDIOMA_UI["actual"]) + ascii_pantalla(self.describir(
                {"name": n.get("tool_name"), "input": n.get("tool_input")}))})
        if resultado.get("subtype") != "success":
            self.enviar({"err": I.msg("problema", self.idioma or IDIOMA_UI["actual"])})
        respuesta = ascii_pantalla(resultado.get("result") or "")[:1500]
        log("<- Claude:", respuesta)
        if respuesta:
            self.enviar({"resp": respuesta})
            self.decir(respuesta)

    # --- permisos pedidos por el hook (hook_permiso.py), para contestarlos en la placa ---
    def vigilar_permisos(self):
        carpeta = CARPETA_ESTADO / "permisos"
        ahora = {f.stem for f in carpeta.glob("*.json")} if carpeta.exists() else set()
        for pid in ahora - self.permisos_enviados:
            try:
                p = json.loads((carpeta / f"{pid}.json").read_text())
            except Exception:
                continue
            self.enviar({"permiso": {k: ascii_pantalla(str(v)) for k, v in p.items()}})
            log("Claude pide permiso:", p.get("herramienta"), p.get("detalle", "")[:60])
            ui = IDIOMA_UI["actual"]
            self.decir(I.msg("permiso_voz", ui, h=p.get("herramienta", "?")), ui)
        for pid in self.permisos_enviados - ahora:     # contestado, o has abierto la tapa
            self.enviar({"permiso_fin": pid})
        self.permisos_enviados = ahora

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
                self.vigilar_permisos()
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
                    ui = IDIOMA_UI["actual"]
                    self.decir(I.msg("listo_voz", ui, p=v.proyecto or I.msg("tu_proyecto", ui)), ui)
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
