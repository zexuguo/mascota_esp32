# /// script
# requires-python = ">=3.11,<3.14"
# dependencies = ["pytr==0.4.10"]
# ///
"""Entrar en Trade Republic UNA vez para que Clawd pueda leer tu cartera (solo lectura).

Uso (en tu terminal, NO a través de Claude):
    cd ~/Projects/mascota_esp32/clawd && uv run tr_login.py

- Pide tu teléfono y tu PIN y te manda la confirmación a la app de Trade Republic.
- El PIN NO se guarda en ningún sitio. Solo se guarda la sesión (cookies) en
  ~/.local/state/clawd/traderepublic/cookies.txt, con permisos solo para ti (600).
- Si un día Clawd dice «sesión caducada», vuelve a ejecutar esto.
- Para cerrar el acceso: borra esa carpeta (y si quieres, cierra la sesión web desde la app).

Usa pytr (https://github.com/pytr-org/pytr, MIT), una librería NO oficial.
"""
import getpass
import os
from pathlib import Path

from pytr.api import TradeRepublicApi

CARPETA = Path.home() / ".local/state/clawd/traderepublic"
COOKIES = CARPETA / "cookies.txt"


def main():
    CARPETA.mkdir(parents=True, exist_ok=True)
    os.chmod(CARPETA, 0o700)
    telefono = input("Teléfono de Trade Republic (formato +34600123456): ").strip()
    pin = getpass.getpass("PIN (no se ve al escribir y no se guarda): ").strip()
    tr = TradeRepublicApi(phone_no=telefono, pin=pin, save_cookies=True, cookies_file=COOKIES,
                          use_v2_login=True)
    del pin
    tr.initiate_weblogin()
    if tr.weblogin_needs_authenticator:
        codigo = input("Código de tu app autenticadora: ").strip()
    else:
        print("Confirma el inicio de sesión en la app de Trade Republic del móvil...")
        codigo = None
    tr.complete_weblogin(codigo)
    if COOKIES.exists():
        os.chmod(COOKIES, 0o600)
    print(f"Hecho. Sesión guardada en {COOKIES} (sin el PIN).")
    print("Reinicia el companion: systemctl --user restart clawd-companion")


if __name__ == "__main__":
    main()
