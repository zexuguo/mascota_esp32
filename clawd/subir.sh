#!/bin/bash
# Sube el código a la placa. El companion (servicio) tiene el puerto USB abierto,
# así que lo para, sube y lo vuelve a arrancar.
cd "$(dirname "$0")" || exit 1
systemctl --user stop clawd-companion 2>/dev/null
pio run -t upload
estado=$?
# esperar a que la placa termine de arrancar: si el companion abre el puerto justo
# mientras se reinicia, la placa puede quedarse parada en modo descarga
sleep 4
systemctl --user start clawd-companion 2>/dev/null
exit $estado
