# Clawd

**El compañero de escritorio para trabajar con Claude Code.** Un cangrejito en una pantalla de
2,8" que te deja apartarte del ordenador sin perderle la pista a Claude.

- **Cierra el portátil y vete.** Claude sigue trabajando (no se suspende) y en Clawd ves en
  qué va: tareas, pasos y tiempo.
- **Contesta permisos desde la placa.** Con la tapa cerrada, lo que Claude quiera hacer sale
  en la pantalla con *Permitir* / *Denegar*.
- **Habla con él.** "Oye Clawd" (o *Hey* / *Ei* / *嘿*) y pregunta lo que quieras; te contesta
  en voz alta, en tu idioma.
- **Callado por defecto.** Solo avisa cuando algo largo termina o estando tú fuera. Nunca repite.

Además: temporizador, pomodoro, juegos y la galaxia de tu vault. Todo en local (voz con
Whisper y Piper), en español, inglés, chino y catalán.

> Demo hecha por Claude para probar ideas: úsala unos días, apunta qué te gusta y qué no,
> y luego hazla tú en `mascota/` con la guía.

## Cuándo avisa (y cuándo no)
| Situación | Qué hace |
|---|---|
| Claude responde algo corto | Nada (Clawd celebra en silencio y sale un punto verde en la barra) |
| Le preguntas por voz | Solo te dice la respuesta |
| Algo de **3 min o más** termina, o termina con la **tapa cerrada** | **Un** aviso con un sonido (la voz, solo con la tapa cerrada) |
| Claude espera tu permiso con la tapa abierta | Punto amarillo y Clawd te saluda; sin sonido (lo ves en la terminal) |
| Con la tapa cerrada pide permiso | La tarjeta de permiso, con un sonido |
| Tu temporizador acaba | Alarma (es lo único que suena aunque esté en silencio) |

## Ponerlo en marcha
El companion es un **servicio** (`clawd-companion`) que arranca solo al iniciar sesión.
```bash
cd ~/Projects/mascota_esp32/clawd
./subir.sh                                  # subir a la placa (para el servicio, sube y lo arranca)
journalctl --user -u clawd-companion -f     # ver lo que hace el companion
systemctl --user restart clawd-companion    # reiniciarlo (p. ej. tras cambiar companion.py)
# a mano, sin servicio:  systemctl --user stop clawd-companion && uv run companion.py --mic
```

## Qué hace
| Tú haces... | Clawd... |
|---|---|
| Tocarlo | Hace una animación de Claude Code al azar |
| Dejar el dedo quieto encima (0,6 s) | Mimos: ojos ^ ^, colorete y se achucha |
| Doble toque | Empieza un pomodoro de 25 min (cinta roja). Otro doble toque lo cancela |
| Tocar el suelo | Va andando hasta allí |
| **Bajar la cortina** (desliza hacia abajo desde arriba; en la mascota, desde cualquier sitio) | Como en el móvil: 8 interruptores (Sonido, Micro, No molestar, Dormir, Tiempo, Oye Clawd, Habla, Carteles), brillo, volumen, avisos (tócalos: la lista entera) y accesos a las 5 pantallas |
| Tocar el panel de abajo | Cambia la información (CPU, RAM, batería) |
| Dar una palmada | Se asusta |
| Poner música (3 s) | Baila, con notas y el LED en arcoíris |
| No hacerle caso 5 min (1 min de noche) | Se duerme y baja el brillo |
| **Mantener pulsado el panel de abajo** (o el botón BOOT) **y hablar** | Te escucha. Al soltar, el PC lo pasa a texto |
| Deslizar a la izquierda / derecha | Pantalla siguiente / anterior: mascota, terminal, galaxia, **juegos** (salen unos puntitos abajo) |

## Hablarle
- **Órdenes cortas** (5 palabras o menos) las hace Clawd solo: "baila", "pomodoro", "duérmete", "despierta", "salta", "silencio", "activa el sonido", "qué hora es", "abre la terminal", "nueva conversación".
- **Todo lo demás se lo pasa a Claude Code** (`claude -p`), que trabaja en tu carpeta personal **con todos los permisos** (`bypassPermissions`): lee, edita, ejecuta comandos, sube código a la placa... sin preguntar. Se le pide que pregunte antes de algo irreversible, pero es una instrucción, no un candado.
- Antes de mandarlo tienes **3 segundos para cancelar** tocando la pantalla (por si Whisper ha entendido otra cosa). Mientras Claude trabaja, toca para pararlo.
- Recuerda la conversación entre órdenes. "Nueva conversación" empieza de cero.
- La voz se pasa a texto **en tu ordenador** con Whisper (modelo `small`), sin mandarla a internet. La última grabación se guarda en `~/.local/state/clawd/ultima_voz.wav` por si quieres oír qué ha captado el micro.
- Para volver a limitarlo: `MODO_PERMISOS` en `companion.py` (`dontAsk` + una lista de `--allowedTools`).

| El PC hace... | Clawd... |
|---|---|
| Claude Code trabajando | Se sienta a teclear con el spinner; abajo, "Cangrejeando... 42 s" |
| Claude Code pide permiso | Agita los brazos, pita y el LED parpadea en naranja |
| Claude Code termina | Confeti y "Listo!" |
| CPU a 85 °C o más | Se pone rojo y suda |
| CPU por encima del 70 % | Anda más rápido |
| Batería por debajo del 20 % sin cargar | Ojos cansados y anda despacio |
| Cada 50 min | Te enseña un cartel: beber agua, estirarte... (tócalo para quitarlo) |

## Cómo está hecho (para cuando lo rehagas)
| Archivo | Qué tiene | Lección de la guía |
|---|---|---|
| `src/placa.h` | Pines y pantalla | 2 |
| `src/clawd.h` | Dibujo del cangrejo (cuadrícula de 16x9 "píxeles gordos"), partículas y bocadillos | 3-4 |
| `src/tactil.h` | Táctil | 5 |
| `src/audio.h` | Sonidos 8 bits y micro, en una **tarea de FreeRTOS** (no congela la animación) | 6-7 |
| `src/pc.h` | Recibir JSON del PC | 9 |
| `src/main.cpp` | El cerebro: modos por prioridad, pomodoro, recordatorios, panel | 8 |
| `companion.py` | Datos del PC y estado de Claude Code (`~/.local/state/zen-claude/estado.json`) | 9 |

## Estilo: una sola rejilla de píxeles
Todo lo que se mueve en la escena (Clawd, corazones, notas, Zzz, gotas, confeti, spinner,
bocadillos y carteles) usa el **mismo píxel de 4 px** (`L` en `clawd.h`) y las posiciones se
redondean a esa rejilla. Clawd no se escala: aplastarse, estirarse y respirar son **fotogramas**
distintos. Los textos de los bocadillos usan una fuente pixel de 3x5 hecha a mano. La interfaz
(barra de arriba, panel y terminal) usa píxel de 2 para los iconos y letra normal para leer bien.

## Las animaciones de Claude Code (`src/animaciones.h`)
Copiadas del propio Claude Code (v2.1.332), donde Clawd es arte de texto: las mismas 15
animaciones, fotograma a fotograma (60 ms cada uno) y con sus nombres:
`jump`, `look`, `celebrate`, `skip`, `spin`, `peekaboo`, `drop`, `waddle`, `peek`, `wink`,
`boop`, `tap`, `sneeze`, `turn` (se da la vuelta entera) y `coin-hop`.

Como en Claude Code: **al arrancar hace una al azar** (la entrada) y **al tocarlo, otra**.
Además: `celebrate` cuando Claude termina, `waddle` al conectarse al PC y alguna suelta cuando
está solo. Por voz: "salta", "gira", "guiña", "estornuda", "escóndete", "celebra", "asómate",
"cáete", "haz la moneda", "pasea", "zapatea". El spinner y sus 186 verbos son también los de Claude Code.

## Juegos (`src/juegos.h`)
Desliza hasta la pantalla de juegos o di "juega" / "a volar" / "a correr".
- **VUELA**: toca para que Clawd aletee; pasa por los huecos entre las columnas de bloques de código.
- **CORRE**: Clawd corre solo; toca para saltar los bugs rojos y las columnas. Los voladores morados
  van a la altura de la cabeza: no saltes. Cada vez va más rápido.
- Los récords se guardan en la placa. En la pantalla de "FIN": toca para otra partida o MENU para volver.

## Dejar a Claude trabajando con el portátil cerrado
Con `companion.py` en marcha:
- **No se suspende mientras Claude trabaja**, aunque cierres la tapa: el companion pide a
  `systemd-logind` que no suspenda (`systemd-inhibit --what=sleep:idle:handle-lid-switch`).
  La sesión se bloquea igual (lo hace Omarchy al cerrar la tapa), pero Claude sigue.
- Al **cerrar la tapa** con Claude trabajando, la placa pasa sola a la pantalla de **progreso**:
  proyecto, verbo y tiempo, lista de tareas (hechas / en curso / pendientes) y los últimos pasos.
  Lo saca del registro de la sesión (`~/.claude/projects/*/*.jsonl`). Por voz: "¿cómo va?".
- **Cuando acaba**: aviso grande en verde ("LISTO!") con el proyecto, lo que ha tardado y su último
  mensaje; LED verde parpadeando y aviso sonoro, que se repite cada 2 min (3 veces). **Toca para cerrarlo.**
  Si te pide permiso: aviso en amarillo ("TE NECESITO").
- Cuando acaba, el portátil **sigue despierto 30 min** si la tapa está cerrada y no has visto el aviso
  (si se suspendiera, la placa se quedaría sin corriente y no te podría avisar). Al tocar el aviso,
  o pasados los 30 min, ya puede suspenderse.

## Diseño (rediseño del 26-11-2026)
- **Color**: base neutra (3 superficies y 3 escalones de texto), el naranja de Claude solo como identidad
  y verde / amarillo / rojo solo para estados. Nada de azules ni morados de adorno.
- **Letras**: dos familias. Píxel 3x5 para títulos y la escena; `efontJA_14/12` (monoespaciada, con tildes) para el texto.
- **Clawd** con las proporciones exactas del de Claude Code (cuerpo 24x16, piernas en las esquinas), sin pupilas ni colorete.
- **Movimiento**: transición deslizante entre pantallas (260 ms, ease-out) con la barra de estado quieta;
  la cortina sigue al dedo; el LED funde en vez de parpadear; el aviso oscurece lo de detrás.
- **Capturas**: el companion puede pedir a la placa una captura (PNG en `~/.local/state/clawd/capturas/`):
  `echo '[{"cmd":"captura"}]' > ~/.local/state/clawd/orden.json`

## Temporizador
Cortina → **Temporiz.**, o por voz: "pon un temporizador de diez minutos", "avísame en media hora",
"cancela el temporizador". El tiempo se elige con una **rueda**, como el selector de hora de Android: primero las **horas** (0-11) y, al soltar, los **minutos** (0-59). El tirador va siempre a donde está el dedo, con un "tic" en cada muesca. Tocando las horas o los minutos del centro vuelves a cada paso. Pausar / seguir / cancelar, y el **Pomodoro** dentro.
Lo que queda se ve en la barra de arriba. Al acabar: aviso "TIEMPO", LED y alarma — **suena aunque el sonido
esté en silencio** (un temporizador que no suena no sirve), como mucho 1 minuto. Tócalo para pararla.

## "Oye Clawd" (en mejora) y la voz de Clawd
- **Oye Clawd** (cortina): la placa solo manda audio al PC cuando nota voz (punto verde en la barra mientras lo hace).
  El PC lo pasa a texto en local y **solo hace caso si empieza por "Oye Clawd"** (o "Clawd" / "Claude");
  si no, lo tira sin guardarlo. "Oye Clawd, pon un temporizador de 5 minutos" lo hace directamente;
  "Oye Clawd" a secas: te escucha y se para sola cuando te callas. **Solo la llamada completa de cada idioma**: "Oye Clawd" (es), "Hey Clawd" (en), "Ei Clawd" (ca), "嘿 Clawd" (zh); "Clawd" a secas o "Hola Claude" no valen. No escucha con "No molestar", música o en los juegos.
- **Habla** (cortina): Clawd dice en voz alta las respuestas de Claude, cuando termina y cuando pide permiso.
  Voz **Piper `es_ES-sharvard-medium`** (en local, `~/.local/share/clawd/voces/`), elegida comparando con Whisper.
  El PC la genera y la manda a la placa en tiempo real (trozos de 30 ms).
- **No molestar**: solo suenan la alarma del temporizador y los permisos; sin carteles, sin voz y sin "Oye Clawd".

## Permisos de Claude Code desde la placa
Hook `PermissionRequest` en `~/.claude/settings.json` → `hook_permiso.py` (copia de antes: `settings.json.bak-clawd-permisos`).
- **Tapa cerrada, sin monitor externo y con el companion en marcha**: la petición sale en la placa
  (herramienta, proyecto y el comando exacto) con **Denegar / Permitir**.
- Tapa abierta (o con monitor externo): no hace nada y te sale el diálogo normal. Si abres la tapa mientras
  espera, vuelve al diálogo normal. Ante cualquier fallo, no decide nada (nunca bloquea a Claude).
- Quitarlo: borrar la entrada `PermissionRequest` de `~/.claude/settings.json`.

## Idiomas: español, inglés, chino y catalán
- **Elegir**: cortina → ⚙ → Ajustes → Idioma (cada idioma escrito en su lengua). La placa lo recuerda.
- **Placa** (`src/idiomas.h`): todos los textos pasan por `T("texto en español")`, que busca la traducción
  en la tabla `TRAD`. En chino se cambia a la letra `efontCN` (chino simplificado); lo que llega del PC se pinta
  **por tramos** (`texto_mixto`): lo chino con la letra china y lo latino con la latina (la china no tiene ñ ni tildes).
  Los títulos de píxeles en chino se pintan con la letra normal.
- **Voz** (`idiomas_pc.py`): Whisper **detecta el idioma** de cada frase (de los cuatro) y Claude responde en el
  mismo. Órdenes, números del temporizador ("ten minutes", "deu minuts", "十五分钟"), "Hey / Ei / 嘿 Clawd",
  mensajes y fecha en cada idioma. Una voz de Piper por idioma (`en_US-lessac`, `zh_CN-huayan`, `ca_ES-upc_ona`,
  `es_ES-sharvard`), comprobadas con Whisper; "Claude" se escribe como se pronuncia en cada una
  (Clod / Clawed / 克劳德 / Clòd) solo para la voz.
- Añadir un texto nuevo: escribirlo con `T("...")` y añadir su fila a `TRAD` en `idiomas.h`.

## Revisión de partidas de ajedrez (chess.com)
- Cada 2 min el companion mira la **API pública de chess.com** (`TU_USUARIO`); al acabar una partida nueva,
  la analiza y sale un aviso discreto. También: **Juegos → Ajedrez** → *Analizar la última*.
- **El portátil no calcula**: las evaluaciones salen de la nube (primero la de **Lichess**, que ya tiene
  analizadas las posiciones típicas; si no, **stockfish.online**). `python-chess` solo lee la partida.
- Clasificación al estilo chess.com por la probabilidad de ganar que pierdes con cada jugada
  (Brillante = sacrificio que es la mejor o casi; Mejor; Excelente < 2 %; Buena < 5 %; Imprecisión < 10 %;
  Error < 20 %; Error grave ≥ 20 %; Teoría; Forzada) y precisión por jugador. Muy parecida a la de chess.com,
  no idéntica (no publican su fórmula). Código en `ajedrez.py` (en el PC) y `src/ajedrez.h` (la pantalla).
- En la placa: resumen (precisión y jugadas por tipo) → *Revisar* (jugada a jugada, flecha verde con la
  mejor) o *Mis errores* (salta solo por tus fallos). Abajo: ‹ atrás · centro = resumen · › adelante.
  Tocar el título (‹ Revisión de partida) vuelve al menú de Juegos.

## El sobremesa (IA local)
- Si el sobremesa está encendido, el panel de la mascota añade una página **Sobremesa**: el modelo de Ollama
  cargado (o "libre"), la temperatura de la GPU y la barra de VRAM usada. Si está apagado, la página no sale.
- Sin instalar nada allí: la API de Ollama por Tailscale (`IP_DEL_SOBREMESA:11434/api/ps`) y `nvidia-smi` por
  SSH (`usuario@sobremesa`), cada 15 s. "Encendido" = contesta el SSH; si Ollama no contesta, sale **IA parada**
  (p. ej. `modo-juego`).
- **Encenderlo desde la placa:** con el sobremesa apagado la página dice *apagado · toca*; al tocarla el companion
  manda el paquete Wake-on-LAN (MAC `MAC_DEL_SOBREMESA`, tarjeta `eno1`) y la barra se pone amarilla hasta
  90 s. Solo funciona **en casa** (misma red): fuera, el paquete no llega al sobremesa.
