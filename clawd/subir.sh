#!/bin/bash
# Sube el código a la placa. El companion (servicio) tiene el puerto USB abierto,
# así que lo para, sube y lo vuelve a arrancar.
cd "$(dirname "$0")" || exit 1
systemctl --user stop clawd-companion 2>/dev/null
pio run -t upload
estado=$?
systemctl --user start clawd-companion 2>/dev/null
exit $estado
