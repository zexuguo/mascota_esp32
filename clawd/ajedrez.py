"""Revisión de partidas de chess.com, al estilo de su "Game Review".

- Las partidas: la API pública de chess.com (sin contraseña, solo el usuario).
- Las evaluaciones: Stockfish EN LA NUBE (stockfish.online). El portátil no calcula nada;
  python-chess solo lee la partida y genera las posiciones.
- La clasificación (Brillante, Mejor, Excelente, Buena, Teoría, Imprecisión, Error, Error grave,
  Forzada) sigue lo que se sabe del sistema de chess.com: cuánta probabilidad de ganar pierdes
  con cada jugada. chess.com no publica su fórmula exacta: saldrá MUY parecida, no idéntica.
"""
import io
import json
import math
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

import chess
import chess.pgn

import config_local as CFG

USUARIO = CFG.CHESS_USUARIO   # en ~/.config/clawd/config.json (fuera del repo)
UA = {"User-Agent": "ClawdCompanion/1.0 (mascota de escritorio)"}
CARPETA = Path.home() / ".local/state/clawd/ajedrez"
PROFUNDIDAD = 14

# Códigos de una letra (la placa recibe la partida entera como una cadena de estas letras)
CLASES = {"K": "teoria", "R": "brillante", "B": "mejor", "E": "excelente", "G": "buena",
          "I": "imprecision", "M": "error", "X": "grave", "F": "forzada"}
VALOR = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3, chess.ROOK: 5, chess.QUEEN: 9, chess.KING: 0}


def _get(url: str, params: dict | None = None, intentos: int = 8):
    if params:
        url += "?" + urllib.parse.urlencode(params)
    for i in range(intentos):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=25) as r:
                return json.loads(r.read())
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None                          # (Lichess: esa posición no la tiene)
            if i == intentos - 1:
                raise
            # 429 = "vas muy deprisa": esperar lo que pide el servicio
            time.sleep(float(e.headers.get("Retry-After") or 2 * (i + 1)) + 0.5)
        except Exception:
            if i == intentos - 1:
                raise
            time.sleep(2 * (i + 1))


# ------------------------------------------------------------------ chess.com
def partidas_recientes() -> list[dict]:
    """Las partidas del último mes (y del anterior, por si acaba de cambiar de mes)."""
    archivos = _get(f"https://api.chess.com/pub/player/{USUARIO}/games/archives").get("archives", [])
    juegos = []
    for url in archivos[-2:]:
        juegos += _get(url).get("games", [])
    return [j for j in juegos if j.get("rules") == "chess" and j.get("pgn")]


# ------------------------------------------------------------------ evaluación en la nube
_cache: dict[str, tuple] = {}


def evaluar(fen: str) -> tuple[float, str | None]:
    """(centipeones desde las BLANCAS, mejor jugada en UCI). El mate cuenta como ±10000."""
    if fen in _cache:
        return _cache[fen]
    # 1) Lichess: posiciones ya analizadas en su nube (aperturas, posiciones típicas), al momento
    try:
        d = _get("https://lichess.org/api/cloud-eval", {"fen": fen, "multiPv": 1}, intentos=2)
    except Exception:
        d = None
    if d and d.get("pvs"):
        pv = d["pvs"][0]
        cp = (10000 - abs(pv["mate"]) * 10) * (1 if pv["mate"] > 0 else -1) if "mate" in pv else pv.get("cp", 0)
        resultado = (float(cp), pv.get("moves", "").split()[0] if pv.get("moves") else None)
        _cache[fen] = resultado
        return resultado
    # 2) Si no la tiene: Stockfish en stockfish.online
    d = _get("https://stockfish.online/api/s/v2.php", {"fen": fen, "depth": PROFUNDIDAD})
    if not d.get("success"):
        raise RuntimeError(d.get("data") or "stockfish.online no ha contestado")
    if d.get("mate") is not None:
        m = int(d["mate"])
        cp = (10000 - abs(m) * 10) * (1 if m > 0 else -1)
    else:
        cp = float(d.get("evaluation") or 0) * 100
    mejor = (d.get("bestmove") or "").split()
    resultado = (cp, mejor[1] if len(mejor) > 1 and mejor[1] != "(none)" else None)
    _cache[fen] = resultado
    time.sleep(0.4)                       # con educación: es un servicio gratuito
    return resultado


def prob_ganar(cp: float) -> float:
    """Centipeones -> % de ganar (la curva que usa Lichess, muy parecida a la de chess.com)."""
    return 50 + 50 * (2 / (1 + math.exp(-0.00368208 * cp)) - 1)


def precision_jugada(perdida: float) -> float:
    return max(0.0, min(100.0, 103.1668 * math.exp(-0.04354 * perdida) - 3.1669))


def es_sacrificio(tablero: chess.Board, jugada: chess.Move) -> bool:
    """¿Deja una pieza "colgando" a cambio de nada inmediato? (para las brillantes)"""
    pieza = tablero.piece_at(jugada.from_square)
    if not pieza or pieza.piece_type in (chess.PAWN, chess.KING):
        return False
    capturada = tablero.piece_at(jugada.to_square)
    ganado = VALOR[capturada.piece_type] if capturada else 0
    despues = tablero.copy()
    despues.push(jugada)
    atacantes = despues.attackers(not pieza.color, jugada.to_square)
    if not atacantes:
        return False
    menor = min(VALOR[despues.piece_at(s).piece_type] or 99 for s in atacantes)
    defendida = bool(despues.attackers(pieza.color, jugada.to_square))
    return VALOR[pieza.piece_type] - ganado >= 2 and (not defendida or menor < VALOR[pieza.piece_type])


def clasificar(tablero: chess.Board, jugada: chess.Move, mejor: str | None, perdida: float,
               gana_despues: float, gana_antes: float, ply: int, en_teoria: bool) -> str:
    if tablero.legal_moves.count() == 1:
        return "F"
    # apertura conocida (aproximación): pronto, sin perder nada y SIN haber salido antes de la
    # teoría (tras un error, lo que viene después ya no es "de libro")
    if en_teoria and ply < 16 and perdida < 1.5:
        return "K"
    acierta = jugada.uci() == mejor or perdida < 2
    if acierta and es_sacrificio(tablero, jugada) and gana_despues >= 50 and gana_antes < 90:
        return "R"
    if jugada.uci() == mejor:
        return "B"
    if perdida < 2:
        return "E"
    if perdida < 5:
        return "G"
    if perdida < 10:
        return "I"
    if perdida < 20:
        return "M"
    return "X"


def analizar(juego: dict, progreso=None) -> dict:
    partida = chess.pgn.read_game(io.StringIO(juego["pgn"]))
    tablero = partida.board()
    yo_blancas = juego["white"]["username"].lower() == USUARIO.lower()
    jugadas = list(partida.mainline_moves())
    fens = [tablero.fen()]
    t = tablero.copy()
    for j in jugadas:
        t.push(j)
        fens.append(t.fen())
    evals = []
    for i, fen in enumerate(fens):
        evals.append(evaluar(fen))
        if progreso:
            progreso((i + 1) / len(fens))
    plies, clases, precision = [], "", {chess.WHITE: [], chess.BLACK: []}
    en_teoria = True
    t = partida.board()
    for i, j in enumerate(jugadas):
        mueve = t.turn
        signo = 1 if mueve == chess.WHITE else -1
        antes = prob_ganar(signo * evals[i][0])
        despues = prob_ganar(signo * evals[i + 1][0])
        perdida = max(0.0, antes - despues)
        mejor = evals[i][1]
        clase = clasificar(t, j, mejor, perdida, despues, antes, i, en_teoria)
        en_teoria = en_teoria and clase == "K"
        precision[mueve].append(precision_jugada(perdida))
        mejor_san = t.san(chess.Move.from_uci(mejor)) if mejor and chess.Move.from_uci(mejor) in t.legal_moves else ""
        san = t.san(j)
        t.push(j)
        cp = evals[i + 1][0]
        plies.append({
            "san": san, "uci": j.uci(), "fen": t.board_fen(), "clase": clase, "mejor": mejor_san,
            "mejor_uci": mejor or "", "eval": (f"M{'+' if cp > 0 else '-'}" if abs(cp) >= 9000 else f"{cp / 100:+.1f}"),
            "num": i // 2 + 1, "blancas": mueve == chess.WHITE,
        })
        clases += clase

    def media(xs):
        return round(sum(xs) / len(xs), 1) if xs else 0.0
    yo = chess.WHITE if yo_blancas else chess.BLACK
    resultado = juego["white" if yo_blancas else "black"]["result"]
    return {
        "id": juego["url"].rsplit("/", 1)[-1], "url": juego["url"], "fin": juego.get("end_time", 0),
        "ritmo": juego.get("time_class", ""), "color": "w" if yo_blancas else "b",
        "rival": juego["black" if yo_blancas else "white"]["username"],
        "res": "gana" if resultado == "win" else
               ("tablas" if resultado in ("agreed", "repetition", "stalemate", "insufficient",
                                          "50move", "timevsinsufficient") else "pierde"),
        "acc": [media(precision[yo]), media(precision[not yo])],
        "clases": clases, "plies": plies, "inicio": partida.board().board_fen(),
    }


def guardar(r: dict):
    CARPETA.mkdir(parents=True, exist_ok=True)
    (CARPETA / f"{r['id']}.json").write_text(json.dumps(r))
    (CARPETA / "ultima.json").write_text(json.dumps(r))


def cargar_ultima() -> dict | None:
    f = CARPETA / "ultima.json"
    return json.loads(f.read_text()) if f.exists() else None


def resumen(r: dict) -> dict:
    """Lo que necesita la placa para la pantalla de resumen (las jugadas, se piden una a una)."""
    mio = 0 if r["color"] == "w" else 1
    cuenta = {}
    for i, c in enumerate(r["clases"]):
        lado = 0 if (i % 2 == mio) else 1          # 0 = tú, 1 = rival
        cuenta.setdefault(c, [0, 0])[lado] += 1
    return {"rival": r["rival"], "color": r["color"], "res": r["res"], "ritmo": r["ritmo"],
            "acc": r["acc"], "clases": r["clases"], "cuenta": cuenta, "n": len(r["plies"])}


if __name__ == "__main__":                        # prueba: analizar la última partida (de más de 20 jugadas)
    j = [x for x in partidas_recientes() if len(x["pgn"].split("\n\n")[-1].split()) > 60][-1]
    print("Analizando", j["url"], "...")
    r = analizar(j, lambda p: print(f"\r{p:4.0%}", end="", flush=True))
    print()
    guardar(r)
    s = resumen(r)
    print("Tú:", s["acc"][0], "% | rival:", s["acc"][1], "% |", s["res"], "vs", s["rival"])
    print({CLASES[k]: v for k, v in s["cuenta"].items()})
    for p in r["plies"][:12]:
        print(f"{p['num']}{'.' if p['blancas'] else '...'} {p['san']:6} {CLASES[p['clase']]:12} mejor: {p['mejor']:6} eval {p['eval']}")
