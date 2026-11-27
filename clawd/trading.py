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


def momentum(par: str, dias: int = 30) -> float | None:
    """Igual que el bot (src/indicadores.momentum): cierre diario de ayer frente al de hace `dias`.
    Velas diarias UTC del mercado real, como las que usa el bot."""
    cuando, m = _cache_velas.get((par, "mom"), (0, None))
    if time.time() - cuando < 600:
        return m
    url = (f"https://www.okx.com/api/v5/market/candles?instId={par.replace('/', '-')}"
           f"&bar=1Dutc&limit={dias + 5}")
    req = urllib.request.Request(url, headers={"User-Agent": "ClawdCompanion/1.0"})
    with urllib.request.urlopen(req, timeout=8) as r:
        cierres = [float(d[4]) for d in reversed(json.loads(r.read()).get("data", []))][:-1]
    m = cierres[-1] / cierres[-1 - dias] - 1 if len(cierres) > dias else None
    _cache_velas[(par, "mom")] = (time.time(), m)
    return m


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

    try:
        mom = momentum(nombre)
    except Exception:
        mom = None
    out = {
        "par": nombre, "bar": bar, "p": precio,
        "mom": round(mom * 100, 1) if mom is not None else None,   # % en 30 días (régimen) "var": round(var, 2), "t0": v[0][0] // 1000,
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


# ------------------------------------------------------------------
# En directo: precio por WebSocket (público, demo de OKX, sin claves)
# y avisos SOLO cuando pasa algo de verdad (KISS): se llena una compra,
# salta un stop o se alcanza un objetivo. Cada aviso, una sola vez.
# ------------------------------------------------------------------
WS_URL = "wss://wspap.okx.com:8443/ws/v5/public"     # la demo (los precios que ve el bot)
AVISADOS = Path.home() / ".local/state/clawd/trading_avisos.json"


class EnVivo:
    def __init__(self, enviar, log=print):
        self.enviar, self.log = enviar, log
        self.precio: dict[str, float] = {}
        self.pos: list = []
        self.pos_leidas = 0.0
        self.avisados = set(_json(AVISADOS, []))

    def _posiciones(self) -> list:
        if time.time() - self.pos_leidas > 30:        # el bot las cambia como mucho cada 30 min
            self.pos = [p for p in _json(BOT / "data/posiciones.json", [])
                        if p.get("estado") in ("abierta", "pendiente")]
            self.pos_leidas = time.time()
        return self.pos

    def _avisar(self, clave: str, titulo: str, sub: str, bueno: bool):
        if clave in self.avisados:
            return
        self.avisados.add(clave)
        try:
            AVISADOS.write_text(json.dumps(sorted(self.avisados)[-200:]))
        except OSError:
            pass
        self.log("trading:", titulo, "·", sub)
        self.enviar({"tr_ev": {"t": titulo, "s": sub, "bueno": int(bueno)}})

    def revisar(self):
        """¿Ha pasado algo con las órdenes del bot al precio de ahora?"""
        for p in self._posiciones():
            precio = self.precio.get(p["par"])
            if not precio:
                continue
            nombre = p["par"].split("/")[0]
            oid = p["orden_id"]
            if p["estado"] == "pendiente" and precio <= p["precio_limite"]:
                self._avisar(f"{oid}:compra", f"{nombre}: se compra",
                             f"llegó a tu orden en {p['precio_limite']:g}", True)
            elif p["estado"] == "abierta":
                if p.get("stop_loss") and precio <= p["stop_loss"]:
                    pnl = _pnl_abierta(p, p["stop_loss"])
                    self._avisar(f"{oid}:stop:{p['stop_loss']}", f"{nombre}: salta el stop",
                                 f"{p['stop_loss']:g} · {pnl:+.1f} USDC", pnl >= 0)
                elif p.get("objetivo") and precio >= p["objetivo"]:
                    pnl = _pnl_abierta(p, p["objetivo"])
                    self._avisar(f"{oid}:objetivo", f"{nombre}: ¡objetivo!",
                                 f"{p['objetivo']:g} · {pnl:+.1f} USDC", True)

    def mensaje_placa(self) -> dict:
        """{"trv": {"p": [BTC, ETH, SOL], "pnl": [...]}} — pnl solo cuenta si hay posición abierta."""
        pnl = [0.0, 0.0, 0.0]
        for p in self._posiciones():
            if p["estado"] == "abierta" and p["par"] in self.precio and p.get("precio_entrada"):
                pnl[PARES.index(p["par"])] = round(_pnl_abierta(p, self.precio[p["par"]]), 2)
        return {"trv": {"p": [self.precio.get(x, 0) for x in PARES], "pnl": pnl}}

    def correr(self):
        import websocket                               # websocket-client
        espera = 5
        while True:
            try:
                ws = websocket.create_connection(WS_URL, timeout=30)
                ws.send(json.dumps({"op": "subscribe", "args": [
                    {"channel": "tickers", "instId": p.replace("/", "-")} for p in PARES]}))
                self.log("trading: precio en directo conectado")
                espera, ultimo_envio, ultimo_ping = 5, 0.0, time.time()
                while True:
                    if time.time() - ultimo_ping > 20:      # OKX cierra a los 30 s sin nada
                        ws.send("ping")
                        ultimo_ping = time.time()
                    m = ws.recv()
                    if m == "pong":
                        continue
                    d = json.loads(m)
                    for t in d.get("data", []):
                        self.precio[t["instId"].replace("-", "/")] = float(t["last"])
                    if time.time() - ultimo_envio >= 1 and self.precio:   # a la placa, 1 vez por segundo
                        ultimo_envio = time.time()
                        self.revisar()
                        self.enviar(self.mensaje_placa())
            except Exception as e:
                self.log(f"trading: precio en directo caído ({e}); reintento en {espera} s")
                time.sleep(espera)
                espera = min(espera * 2, 300)


# ------------------------------------------------------------------
# Tabla de operaciones y ficha de cada una (con sus cálculos y razonamiento)
# La placa pide "tr:ops" (la tabla) y "tr:op:<orden_id>" (una ficha).
# ------------------------------------------------------------------
BAR_MS = {"1H": 3600_000, "4H": 4 * 3600_000, "1Dutc": 86400_000}


def _es(x: float, dec: int = 2) -> str:
    """Número a la española: 1.655,00"""
    s = f"{x:,.{dec}f}"
    return s.replace(",", "X").replace(".", ",").replace("X", ".")


def _pc(x: float, dec: int = 1) -> str:
    """+5,3 % (con signo y coma)."""
    return f"{x:+.{dec}f}".replace(".", ",")


def _precio(x: float) -> str:
    return _es(x, 0 if x >= 10000 else 1 if x >= 1000 else 2)


def _fecha(ms: int | None) -> str:
    import datetime
    if not ms:
        return "?"
    return datetime.datetime.fromtimestamp(ms / 1000).strftime("%d/%m %H:%M")


def _eventos_log() -> dict:
    """Del log del bot: cuándo se cerró cada orden, y la Time Machine, momentum y escéptico."""
    import datetime
    cierres, tm, mom = {}, [], []
    for f in sorted((BOT / "logs").glob("20*.jsonl")):
        try:
            lineas = f.read_text().splitlines()
        except OSError:
            continue
        for l in lineas:
            if '"cerrada"' not in l and '"timemachine"' not in l and '"momentum"' not in l:
                continue
            try:
                d = json.loads(l)
            except ValueError:
                continue
            ts = int(datetime.datetime.fromisoformat(d["ts"]).timestamp() * 1000)
            if d.get("accion") == "cerrada" and "posicion" in d:
                cierres.setdefault(d["posicion"]["orden_id"], ts)
            elif d.get("accion") == "timemachine" and "inclinacion" in d:
                tm.append((ts, d))
            elif d.get("accion") == "momentum":
                mom.append((ts, d.get("momentos", {})))
    return {"cierres": cierres, "tm": tm, "mom": mom}


def _r(p: dict) -> tuple[float | None, float | None]:
    """(R planeada, R conseguida). R = lo que se arriesgaba hasta el stop inicial."""
    e = p.get("precio_entrada")
    sl = p.get("stop_loss_inicial") or p.get("stop_loss")
    if not e or not sl or sl >= e:
        return None, None
    riesgo = p["cantidad"] * (e - sl)
    plan = (p["objetivo"] - e) / (e - sl) if p.get("objetivo") else None
    hecho = p["pnl"] / riesgo if p.get("pnl") is not None and riesgo > 0 else None
    return plan, hecho


def operaciones() -> list:
    """Filas de la tabla, la más nueva primero: [n, par, fecha, estado, pnl, R].
    n = posición en posiciones.json (una venta en dos partes comparte orden_id)."""
    todas = _json(BOT / "data/posiciones.json", [])
    pos = [(n, p) for n, p in enumerate(todas)
           if p.get("estado") in ("cerrada", "abierta", "pendiente", "huérfana")]
    pos.sort(key=lambda np_: (np_[1].get("ts") or 0, np_[0]), reverse=True)
    filas = []
    for n, p in pos[:40]:
        _, r_hecho = _r(p)
        estado = {"cerrada": "c", "abierta": "a", "pendiente": "p"}.get(p["estado"], "h")
        filas.append([n, PARES.index(p["par"]) if p["par"] in PARES else 0,
                      _fecha(p.get("ts"))[:5], estado, round(p.get("pnl") or 0, 2),
                      round(r_hecho, 2) if r_hecho is not None else None])
    return filas


def _velas_hasta(par: str, bar: str, fin_ms: int, n: int = 48) -> list:
    url = (f"https://www.okx.com/api/v5/market/history-candles?instId={par.replace('/', '-')}"
           f"&bar={bar}&limit={n}&after={fin_ms}")
    req = urllib.request.Request(url, headers={"x-simulated-trading": "1", "User-Agent": "ClawdCompanion/1.0"})
    with urllib.request.urlopen(req, timeout=10) as r:
        datos = json.loads(r.read()).get("data", [])
    return [[int(d[0]), float(d[1]), float(d[2]), float(d[3]), float(d[4])] for d in reversed(datos)]


def operacion(n: int) -> tuple[dict, dict]:
    """La ficha de la operación n: (cálculos + gráfica, texto del razonamiento). Dos mensajes."""
    todas = _json(BOT / "data/posiciones.json", [])
    if not 0 <= n < len(todas):
        return {"id": n, "err": "no existe"}, {"id": n, "t": ""}
    p = todas[n]
    orden_id = p["orden_id"]
    ev = _eventos_log()
    par, base = p["par"], p["par"].split("/")[0]
    e = p.get("precio_entrada")
    lim = p["precio_limite"]
    sl0 = p.get("stop_loss_inicial") or p.get("stop_loss")
    tp = p.get("objetivo")
    t_orden = p.get("ts") or 0
    t_cierre = p.get("ts_cierre") or ev["cierres"].get(orden_id)
    ahora = int(time.time() * 1000)

    # la gráfica: elegir el tamaño de vela para que quepa todo el trade
    fin = (t_cierre or ahora)
    dur_h = max(1, (fin - t_orden) / 3600_000)
    bar = "1H" if dur_h <= 36 else "4H" if dur_h <= 36 * 4 else "1Dutc"
    ms = BAR_MS[bar]
    fin_graf = min(ahora + ms, fin + 6 * ms)
    try:
        v = _velas_hasta(par, bar, fin_graf)
    except Exception:
        v = []
    i_orden = next((i for i, x in enumerate(v) if x[0] + ms > t_orden), -1)
    i_compra = -1
    if e and i_orden >= 0:
        i_compra = next((i for i in range(i_orden, len(v)) if v[i][3] <= e), i_orden)
    i_venta = next((i for i, x in enumerate(v) if t_cierre and x[0] + ms > t_cierre), -1)

    # los cálculos, ya hechos: [etiqueta, valor, color] (color: 0 normal, 1 verde, 2 rojo, 3 amarillo)
    calc = []
    estado = {"cerrada": "cerrada", "abierta": "abierta", "pendiente": "orden esperando",
              "huérfana": "huérfana (revisar)"}.get(p["estado"], p["estado"])
    calc.append(["Estado", estado, 0])
    calc.append(["Orden puesta", _fecha(t_orden), 0])
    if t_cierre:
        h = (t_cierre - t_orden) / 3600_000
        calc.append(["Cerrada", f"{_fecha(t_cierre)} ({h:.0f} h)" if h < 72 else f"{_fecha(t_cierre)} ({h / 24:.1f} d)".replace(".", ","), 0])
    calc.append(["Precio límite", _precio(lim), 3])
    if e:
        calc.append(["Entrada", _precio(e), 0])
    ref = e or lim
    if sl0:
        calc.append(["Stop inicial", f"{_precio(sl0)} ({_pc(100 * (sl0 / ref - 1))} %)", 2])
    if p.get("stop_loss") and sl0 and p["stop_loss"] != sl0:
        calc.append(["Stop subido a la entrada", _precio(p["stop_loss"]), 3])
    if tp:
        calc.append(["Objetivo", f"{_precio(tp)} ({_pc(100 * (tp / ref - 1))} %)", 1])
    if sl0 and tp and ref > sl0:
        r_plan = (tp - ref) / (ref - sl0)
        calc.append(["R prevista (premio/riesgo)", f"{_es(r_plan)} R", 0])
    q = p.get("cantidad") or 0
    coste = p.get("coste") or (q * ref)
    calc.append(["Tamaño", f"{q:g} {base} = {_es(coste)} USDC".replace(".", ",", 1), 0])
    if sl0 and ref > sl0:
        riesgo = q * (ref - sl0) + coste * 0.002
        calc.append(["Riesgo si salta el stop", f"{_es(riesgo)} USDC", 2])
    if p.get("precio_salida"):
        calc.append(["Salida", f"{_precio(p['precio_salida'])} · {p.get('motivo_cierre') or '?'}", 0])
        com = coste * 0.001 + (p.get("ingreso") or coste) * 0.001
        calc.append(["Comisiones (0,1 % × 2)", f"≈ {_es(com)} USDC", 0])
    if p.get("pnl") is not None:
        pnl = p["pnl"]
        calc.append(["Resultado", f"{_pc(pnl, 2)} USDC ({_pc(100 * pnl / coste, 2)} %)", 1 if pnl > 0 else 2])
        _, r_hecho = _r(p)
        if r_hecho is not None:
            calc.append(["R conseguida", f"{_pc(r_hecho, 2)} R", 1 if r_hecho > 0 else 2])
    # lo que vieron los filtros en código (solo existe desde el 27/11)
    tm = [d for ts, d in ev["tm"] if d.get("par") == par and 0 <= t_orden - ts < 15 * 60_000]
    if tm:
        d = tm[-1]
        calc.append(["Time Machine", f"obj {100 * d['p_objetivo']:.0f} % · stop {100 * d['p_stop']:.0f} % · "
                                     f"{_pc(100 * d['inclinacion'])} pts", 1 if d["inclinacion"] >= -0.05 else 2])
    mm = [m for ts, m in ev["mom"] if 0 <= t_orden - ts < 15 * 60_000 and m.get(par) is not None]
    if mm:
        calc.append(["Momentum 30 d", f"{_pc(100 * mm[-1][par])} %", 1 if mm[-1][par] > 0 else 2])

    marcas = []
    if i_compra >= 0 and e:
        marcas.append([i_compra, "c", e, 0])
    if i_venta >= 0 and p.get("precio_salida"):
        marcas.append([i_venta, "v", p["precio_salida"], round(p.get("pnl") or 0, 1)])
    ficha = {"id": n, "par": PARES.index(par) if par in PARES else 0, "bar": bar,
             "est": p["estado"][0], "e": e or lim, "sl": sl0 or 0, "tp": tp or 0,
             "slf": p.get("stop_loss") or 0, "i": i_compra if e else i_orden,
             "pnl": round(p.get("pnl") or 0, 2),
             "v": [[o, h, l, c] for _, o, h, l, c in v], "m": marcas, "calc": calc}
    while len(json.dumps({"tr_op": ficha}, ensure_ascii=False).encode()) > 3500 and len(ficha["v"]) > 16:
        ficha["v"] = ficha["v"][4:]
        ficha["i"] = max(-1, ficha["i"] - 4)
        ficha["m"] = [[i - 4, *r] for i, *r in ficha["m"] if i >= 4]

    # el razonamiento, en texto
    partes = []
    if p.get("motivo_entrada"):
        partes.append("§Por qué compró (Claude)\n" + p["motivo_entrada"])
    if p.get("motivo_cierre"):
        cierre = p["motivo_cierre"] + (f". {p['nota_cierre']}" if p.get("nota_cierre") else "")
        partes.append("§Cómo se cerró\n" + cierre)
    esc = [r for r in _json(BOT / "data/esceptico.json", []) if r.get("orden_id") == orden_id]
    if esc:
        partes.append(f"§Escéptico ({esc[-1]['veredicto']}, {esc[-1]['confianza']})\n" + esc[-1]["argumento"])
    texto = "\n".join(partes)
    while len(json.dumps({"tr_opt": {"id": n, "t": texto}}, ensure_ascii=False).encode()) > 3500:
        texto = texto[:-60].rsplit(" ", 1)[0] + "…"
    return ficha, {"id": n, "t": texto}
