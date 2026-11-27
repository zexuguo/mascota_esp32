"""Datos privados de Clawd (IPs, MAC, usuarios): NO van en el repo (es público).

Viven en ~/.config/clawd/config.json (copia config.ejemplo.json ahí y rellénalo).
Si falta el archivo o un dato, la función que lo usa simplemente se apaga.
"""
import json
from pathlib import Path

RUTA = Path.home() / ".config/clawd/config.json"


def _leer() -> dict:
    try:
        return json.loads(RUTA.read_text())
    except FileNotFoundError:
        return {}
    except (OSError, ValueError) as e:
        print(f"Clawd: no se pudo leer {RUTA}: {e}", flush=True)
        return {}


_c = _leer()
SOBREMESA_IP = _c.get("sobremesa_ip", "")     # IP de Tailscale (con el nombre, Ollama contesta 403)
SOBREMESA_MAC = _c.get("sobremesa_mac", "")   # tarjeta de red por cable, para Wake-on-LAN
SOBREMESA_SSH = _c.get("sobremesa_ssh", "")   # "usuario@máquina" para ver la GPU con nvidia-smi
CHESS_USUARIO = _c.get("chess_usuario", "")   # tu usuario de chess.com
