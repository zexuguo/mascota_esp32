"""El bot de trading (~/Projects/trading-bot) en la pantalla de Clawd.

SOLO LECTURA: lee los archivos del bot (posiciones, planes, logs) y pide velas a la API
pública de OKX. No usa claves, no toca el bot y no puede mandar órdenes.

Lo que se manda a la placa (una línea JSON, < 3,5 KB porque la placa corta a 4 KB):
  {"tr_res": {...}}   resumen para el panel de la mascota (cada minuto)
  {"tr": {...}}       un par con su gráfica, al abrir la pantalla o cambiar de par
"""
import json
import time
import urllib.request
from pathlib import Path

BOT = Path.home() / "Projects/trading-bot"
PARES = ("BTC/USDC", "ETH/USDC", "SOL/USDC")
N_VELAS = 48                     # 48 velas: 2 días en 1 h, 8 días en 4 h
VELA_MS = {"1H": 3600_000, "4H": 4 * 3600_000}

_cache_velas: dict = {}          # (par, vela) -> (cuándo, velas)


def disponible() -> bool:
    return (BOT / "data/posiciones.json").exists()


def _json(ruta: Path, defecto):
    try:
        return json.loads(ruta.read_text())
    except (OSError, ValueError):
        return defecto


def velas(par: str, bar: str = "1H") -> list:
    """[[ts_ms, o, h, l, c], ...] de la más antigua a la más nueva.
    Con la cabecera de "demo" OKX da los precios de la cuenta demo, que son los que ve el bot."""
    cuando, v = _cache_velas.get((par, bar), (0, []))
    if time.time() - cuando < 25:
        return v
    url = (f"https://www.okx.com/api/v5/market/candles?instId={par.replace('/', '-')}"
           f"&bar={bar}&limit={N_VELAS}")
    req = urllib.request.Request(url, headers={"x-simulated-trading": "1",
                                               "User-Agent": "ClawdCompanion/1.0"})
    with urllib.request.urlopen(req, timeout=8) as r:
        datos = json.loads(r.read()).get("data", [])
    v = [[int(d[0]), float(d[1]), float(d[2]), float(d[3]), float(d[4])] for d in reversed(datos)]
    _cache_velas[(par, bar)] = (time.time(), v)
    return v


def ultima_decision_log() -> dict:
    """Capital y exposición del último ciclo (vienen en cada decisión del log)."""
    logs = sorted((BOT / "logs").glob("20*.jsonl"))
    for f in reversed(logs[-3:]):
        try:
            lineas = f.read_text().splitlines()
        except OSError:
            continue
        for l in reversed(lineas):
            if '"decision"' in l and '"capital"' in l:
                try:
                    return json.loads(l)
                except ValueError:
                    pass
    return {}


def _pnl_abierta(p: dict, precio: float) -> float:
    """Lo que ganaría o perdería si vendiera ahora (con el 0,1 % de comisión de cada lado)."""
    q = p.get("cantidad") or 0
    return q * precio * 0.999 - (p.get("coste") or q * p["precio_entrada"] * 1.001)


def resumen(precios: dict | None = None) -> dict:
    """Para el panel: PnL de todo lo cerrado, % sobre el capital, abiertas y pendientes."""
    pos = _json(BOT / "data/posiciones.json", [])
    cerradas = [p for p in pos if p.get("estado") == "cerrada" and p.get("pnl") is not None]
    pnl = sum(p["pnl"] for p in cerradas)
    completas = [p for p in cerradas if not p.get("forzada") and not p.get("parcial")
                 and not p.get("tuvo_parcial")]          # igual que src/metricas.py del bot
    ganadoras = sum(1 for p in completas if p["pnl"] > 0)
    capital = ultima_decision_log().get("capital") or 0
    abiertas = [p for p in pos if p.get("estado") == "abierta"]
    flotante, prog = 0.0, 0.0
    for p in abiertas:
        if precios and p["par"] in precios and p.get("precio_entrada"):
            precio = precios[p["par"]]
            flotante += _pnl_abierta(p, precio)
            sl = p.get("stop_loss_inicial") or p.get("stop_loss")
            if sl and p.get("objetivo") and p["objetivo"] > sl:   # 0 = en el stop, 1 = en el objetivo
                prog = max(0.0, min(1.0, (precio - sl) / (p["objetivo"] - sl)))
    # ¿vivo? el temporizador lo despierta cada 30 min y cada despertar escribe en el log del día
    # (ultimo_ciclo.txt NO sirve: solo cambia en el ciclo completo, cada 4 h)
    logs = sorted((BOT / "logs").glob("20*.jsonl"))
    hace = int((time.time() - logs[-1].stat().st_mtime) / 60) if logs else -1
    stop = (BOT / "STOP").exists()
    return {
        "pnl": round(pnl, 2),
        "pct": round(100 * pnl / capital, 2) if capital else 0,
        "flot": round(flotante, 2),
        "cap": round(capital),
        "n": len(completas),
        "acierto": round(100 * ganadoras / len(completas)) if completas else 0,
        "ab": len(abiertas),
        "prog": round(prog, 3),
        "pend": sum(1 for p in pos if p.get("estado") == "pendiente"),
        "hace": hace,                                  # minutos desde que el bot escribió en su log
        "ok": int(not stop and 0 <= hace < 75),        # 2 despertares seguidos sin escribir = parado
    }


def _indice(ts: int, v: list, ms: int) -> int:
    """En qué vela cae un instante (-1 si es anterior a la gráfica)."""
    if not v or ts < v[0][0]:
        return -1
    return min(len(v) - 1, (ts - v[0][0]) // ms)


def _vela_compra(p: dict, v: list, ms: int) -> int:
    """La orden es limit: se llenó en la primera vela (tras ponerla) que bajó hasta su precio.
    Si se puso antes de que empiece la gráfica, no se sabe dónde se llenó: -1 (sin marca)."""
    e = p.get("precio_entrada") or p.get("precio_limite")
    desde = p.get("ts") or 0
    if not v or desde < v[0][0]:
        return -1
    for i, (ts, o, h, l, c) in enumerate(v):
        if ts + ms > desde and l <= e:
            return i
    return -1


def recortar(texto: str, n: int) -> str:
    texto = " ".join((texto or "").split())
    return texto if len(texto) <= n else texto[:n - 1].rsplit(" ", 1)[0] + "…"


def par(nombre: str, bar: str = "1H") -> dict:
    """Todo lo que dibuja la pantalla de un par: velas, posición, marcas y la última decisión."""
    bar = bar if bar in VELA_MS else "1H"
    ms = VELA_MS[bar]
    v = velas(nombre, bar)
    if not v:
        return {"par": nombre, "bar": bar, "err": "sin velas"}
    precio = v[-1][4]
    pos = [p for p in _json(BOT / "data/posiciones.json", []) if p.get("par") == nombre]

    # la posición viva (abierta) o, si no, la orden limit esperando
    viva = {}
    for p in pos:
        if p.get("estado") == "abierta" and p.get("precio_entrada"):
            viva = {"st": "ab", "e": p["precio_entrada"], "sl": p.get("stop_loss"),
                    "tp": p.get("objetivo"), "sl0": p.get("stop_loss_inicial") or p.get("stop_loss"),
                    "pnl": round(_pnl_abierta(p, precio), 2), "i": _vela_compra(p, v, ms)}
            break
    if not viva:
        for p in pos:
            if p.get("estado") == "pendiente":
                viva = {"st": "pe", "e": p["precio_limite"], "sl": p.get("stop_loss"),
                        "tp": p.get("objetivo"), "i": _indice(p.get("ts") or 0, v, ms)}
                break

    # marcas de compra (c) y venta (v) que caen dentro de la gráfica: [vela, tipo, precio, pnl]
    marcas = []
    for p in pos:
        if not p.get("precio_entrada"):
            continue
        i = _vela_compra(p, v, ms)
        if i >= 0:
            marcas.append([i, "c", p["precio_entrada"], 0])
        if p.get("estado") == "cerrada" and p.get("ts_cierre") and p.get("precio_salida"):
            j = _indice(p["ts_cierre"], v, ms)
            if j >= 0:
                marcas.append([j, "v", float(f"{p['precio_salida']:.5g}"), round(p.get("pnl") or 0, 1)])
    marcas = marcas[-8:]

    plan = _json(BOT / "data/planes.json", {}).get(nombre, {})
    hace = -1
    if plan.get("ts"):
        try:
            import datetime
            t = datetime.datetime.fromisoformat(plan["ts"])
            hace = int((time.time() - t.timestamp()) / 60)
        except ValueError:
            pass
    atras = 24 if bar == "1H" else 6                                # cambio en 24 h
    var = 100 * (precio / v[-atras - 1][4] - 1) if len(v) > atras else 0

    out = {
        "par": nombre, "bar": bar, "p": precio, "var": round(var, 2), "t0": v[0][0] // 1000,
        "v": [[o, h, l, c] for _, o, h, l, c in v],
        "pos": viva, "m": marcas,
        "dec": {"acc": plan.get("accion", ""), "hace": hace,
                "plan": recortar(plan.get("plan") or plan.get("razonamiento") or "", 230)},
    }
    # la placa corta las líneas de más de 4 KB: si no cabe, menos velas
    while len(json.dumps({"tr": out})) > 3500 and len(out["v"]) > 24:
        quita = 4
        out["v"] = out["v"][quita:]
        out["t0"] += quita * ms // 1000
        out["m"] = [[i - quita, *r] for i, *r in out["m"] if i >= quita]
        if out["pos"].get("i", -1) >= 0:
            out["pos"]["i"] = max(-1, out["pos"]["i"] - quita)
    return out


if __name__ == "__main__":              # probar a mano: uv run python trading.py
    import sys
    nombre = sys.argv[1] if len(sys.argv) > 1 else "ETH/USDC"
    d = par(nombre, sys.argv[2] if len(sys.argv) > 2 else "1H")
    print(json.dumps({**d, "v": f"{len(d.get('v', []))} velas"}, ensure_ascii=False, indent=1))
    print("bytes:", len(json.dumps({"tr": d})))
    print(json.dumps(resumen({nombre: d.get("p", 0)}), indent=1))
