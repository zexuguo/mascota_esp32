#!/usr/bin/env python3
"""Hook PermissionRequest de Claude Code: pedir el permiso en la placa de Clawd.

Claude Code lo ejecuta justo antes de enseñar el diálogo de "¿permito esto?".
- Si la tapa del portátil está ABIERTA: no hace nada (sale al momento) y te
  aparece el diálogo de siempre en la terminal.
- Si está CERRADA, sin monitor externo y con el companion en marcha: deja la petición en
  ~/.local/state/clawd/permisos/ y espera a que la contestes en la placa.
  Si abres la tapa mientras espera, la retira y vuelves al diálogo normal.

Contestar: {"hookSpecificOutput": {"hookEventName": "PermissionRequest",
            "decision": {"behavior": "allow"}}}   (o "deny" con "message")
No contestar nada = Claude Code pregunta como siempre. Ante cualquier error,
no contesta: este hook nunca debe bloquear a Claude.
"""
import json
import sys
import time
import uuid
from pathlib import Path

ESTADO = Path.home() / ".local/state/clawd"
CARPETA = ESTADO / "permisos"
ESPERA_MAX = 10 * 60          # segundos (el hook tiene timeout de 15 min en settings.json)


def tapa_cerrada() -> bool:
    try:
        f = next(iter(Path("/proc/acpi/button/lid").glob("*/state")))
        return "closed" in f.read_text()
    except Exception:
        return False


def monitor_externo() -> bool:
    """Tapa cerrada + monitor externo = estás trabajando (modo "clamshell"): no usar la placa."""
    for estado in Path("/sys/class/drm").glob("card*-*/status"):
        nombre = estado.parent.name
        if "eDP" in nombre or "LVDS" in nombre:      # la pantalla del propio portátil
            continue
        try:
            if estado.read_text().strip() == "connected":
                return True
        except Exception:
            pass
    return False


def companion_vivo() -> bool:
    try:
        return time.time() - (ESTADO / "vivo").stat().st_mtime < 5   # lo toca cada segundo
    except Exception:
        return False


def detalle(herramienta: str, e: dict) -> str:
    if herramienta == "Bash":
        return "$ " + e.get("command", "")
    for clave in ("file_path", "notebook_path", "path", "url", "pattern", "query"):
        if e.get(clave):
            return str(e[clave])
    return json.dumps(e, ensure_ascii=False)[:200]


def main():
    datos = json.load(sys.stdin)
    if not tapa_cerrada() or monitor_externo() or not companion_vivo():
        return                                    # diálogo normal
    CARPETA.mkdir(parents=True, exist_ok=True)
    pid = uuid.uuid4().hex[:8]
    peticion = CARPETA / f"{pid}.json"
    respuesta = CARPETA / f"{pid}.respuesta"
    herramienta = datos.get("tool_name", "?")
    peticion.write_text(json.dumps({
        "id": pid,
        "herramienta": herramienta,
        "detalle": detalle(herramienta, datos.get("tool_input") or {})[:300],
        "proyecto": Path(datos.get("cwd") or "~").name,
    }, ensure_ascii=False))
    try:
        fin = time.time() + ESPERA_MAX
        while time.time() < fin:
            if respuesta.exists():
                ok = json.loads(respuesta.read_text()).get("ok")
                decision = {"behavior": "allow"} if ok else \
                           {"behavior": "deny", "message": "Denegado desde la placa de Clawd"}
                print(json.dumps({"hookSpecificOutput": {"hookEventName": "PermissionRequest",
                                                         "decision": decision}}))
                return
            if not tapa_cerrada():                # has abierto el portátil: a la terminal
                return
            time.sleep(0.5)
    finally:
        peticion.unlink(missing_ok=True)
        respuesta.unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        pass                                      # nunca bloquear a Claude
    sys.exit(0)
