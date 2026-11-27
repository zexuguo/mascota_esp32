"""Tu cartera de Trade Republic en Clawd (SOLO LECTURA).

Usa la sesión que guarda `tr_login.py` (cookies, sin PIN). Nunca intenta iniciar sesión por su
cuenta: si la sesión ha caducado, avisa y espera a que vuelvas a ejecutar `uv run tr_login.py`
(así no te llegan códigos al móvil sin que lo pidas). Solo pide la cartera, el efectivo, los
precios y el historial del valor: ninguna función que opere.

Manda a la placa {"trp": {...}} (< 3,5 KB): total, invertido, efectivo, ganancia frente a lo que
pagaste, cambio de hoy (si Trade Republic da el cierre de ayer), las posiciones y una gráfica.
"""
import asyncio
import json
import time
from decimal import Decimal
from pathlib import Path

CARPETA = Path.home() / ".local/state/clawd/traderepublic"
COOKIES = CARPETA / "cookies.txt"
HISTORIA = CARPETA / "historia.json"   # [[segundos, total], ...] que va guardando Clawd
DIAS_HISTORIA = 31
CADA_S = 30 * 60                # un punto cada 30 min


def _num(x) -> float:
    try:
        return float(Decimal(str(x)))
    except Exception:
        return 0.0


def _apuntar(total: float) -> list:
    """Guarda el total en la historia local (solo para ti: 600) y la devuelve."""
    import os
    try:
        hist = json.loads(HISTORIA.read_text())
    except (OSError, ValueError):
        hist = []
    ahora = int(time.time())
    if not hist or ahora - hist[-1][0] >= CADA_S:
        hist.append([ahora, round(total, 2)])
    else:
        hist[-1] = [hist[-1][0], round(total, 2)]          # el último punto, al día
    hist = [h for h in hist if ahora - h[0] <= DIAS_HISTORIA * 86400]
    nuevo = HISTORIA.with_suffix(".tmp")
    nuevo.write_text(json.dumps(hist))
    os.chmod(nuevo, 0o600)
    nuevo.replace(HISTORIA)
    return hist


async def _leer(tr) -> dict:
    from pytr.tickers import fetch_instrument_details, fetch_tickers
    await tr.compact_portfolio()
    await tr.cash()
    posiciones, efectivo = [], []
    faltan = 2
    while faltan:
        sid, sub, resp = await asyncio.wait_for(tr.recv(), timeout=30)
        tipo = sub["type"]
        if tipo == "compactPortfolioByType":
            for cat in resp.get("categories", []):
                for p in cat.get("positions", []):
                    p.setdefault("instrumentId", p.get("isin"))
                    posiciones.append(p)
            faltan -= 1
        elif tipo == "cash":
            efectivo = resp
            faltan -= 1
        await tr.unsubscribe(sid)
    await fetch_instrument_details(tr, posiciones)
    await fetch_tickers(tr, posiciones)
    # por posición: el cierre de ayer (para el «hoy»). Si una petición falla, se sigue con lo
    # demás (Trade Republic cambia sus temas sin avisar: los de historial ya no existen).
    for p in posiciones:
        if "price" not in p or not p.get("exchangeIds"):
            continue
        bolsa = p["exchangeIds"][0]
        try:
            sid = await tr.ticker(p["instrumentId"], bolsa)
            _, _, resp = await asyncio.wait_for(tr.recv(), timeout=10)
            p["_ayer"] = (resp.get("pre") or {}).get("price")
            await tr.unsubscribe(sid)
        except Exception:
            pass
    await tr.close()
    return {"posiciones": posiciones, "efectivo": efectivo}


def leer() -> dict:
    """La cartera para la placa. {"ok": 0, "err": ...} si no hay sesión."""
    if not COOKIES.exists():
        return {"ok": 0, "err": "sin sesión"}
    from pytr.api import TradeRepublicApi
    # teléfono y PIN de relleno: solo se usan para INICIAR sesión, y aquí nunca se inicia
    tr = TradeRepublicApi(phone_no="-", pin="-", save_cookies=True, cookies_file=COOKIES,
                          use_v2_login=True)
    if not tr.resume_websession():
        return {"ok": 0, "err": "sesión caducada"}
    datos = asyncio.run(_leer(tr))

    pos = []
    for p in datos["posiciones"]:
        if "price" not in p:
            continue
        q, precio = _num(p.get("netSize")), _num(p["price"])
        coste = q * _num(p.get("averageBuyIn"))
        ayer = _num(p.get("_ayer")) * q if p.get("_ayer") else None
        pos.append({"n": (p.get("name") or p["instrumentId"])[:16], "v": q * precio, "c": coste, "a": ayer})
    pos.sort(key=lambda x: x["v"], reverse=True)
    invertido = sum(p["v"] for p in pos)
    coste = sum(p["c"] for p in pos)
    efectivo = sum(_num(c.get("amount")) for c in datos["efectivo"]) if isinstance(datos["efectivo"], list) else 0.0
    con_ayer = [p for p in pos if p["a"]]
    dia = sum(p["v"] - p["a"] for p in con_ayer) if con_ayer else None
    base_dia = sum(p["a"] for p in con_ayer) if con_ayer else None
    puntos = _apuntar(invertido + efectivo)
    hist = [v for _, v in puntos]
    if len(hist) > 48:                                   # 48 puntos como mucho
        paso = len(hist) / 48
        hist = [hist[int(i * paso)] for i in range(47)] + [hist[-1]]
    dias = (puntos[-1][0] - puntos[0][0]) / 86400 if len(puntos) > 1 else 0
    out = {
        "ok": 1, "t": int(time.time()),
        "total": round(invertido + efectivo, 2), "inv": round(invertido, 2), "cash": round(efectivo, 2),
        "gan": round(invertido - coste, 2), "gan_pct": round(100 * (invertido / coste - 1), 2) if coste else 0,
        "dia": round(dia, 2) if dia is not None else None,
        "dia_pct": round(100 * dia / base_dia, 2) if dia is not None and base_dia else None,
        # [nombre, valor, % ganado frente a lo pagado, peso en la cartera]
        "pos": [[p["n"], round(p["v"], 2), round(100 * (p["v"] / p["c"] - 1), 1) if p["c"] else 0,
                 round(p["v"] / invertido, 3) if invertido else 0] for p in pos[:10]],
        "otras": len(pos) - min(len(pos), 10),
        "hist": [round(x, 2) for x in hist], "rango": round(dias, 1),   # días que abarca la gráfica
    }
    return out


if __name__ == "__main__":       # probar: uv run --with pytr==0.4.10 python traderepublic.py
    d = leer()
    print(json.dumps({k: v for k, v in d.items() if k not in ("pos", "hist")}, indent=1))
    print("posiciones:", len(d.get("pos", [])), "· puntos de la gráfica:", len(d.get("hist", [])),
          "· bytes:", len(json.dumps({"trp": d}, ensure_ascii=False).encode()))
