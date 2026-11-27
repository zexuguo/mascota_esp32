"""
Marco protector (tipo "bumper") para la placa LCDWiki ES3C28P.
Protege esquinas y bordes; deja a la vista la pantalla, la parte de atrás
y casi todos los laterales (puertos y botones accesibles sin saber dónde están).

Uso:  uv run --with manifold3d --with numpy --with matplotlib marco.py
Sale: marco.stl  (+ marco.png, una vista previa)

Cambia SOLO los números de PARÁMETROS si la placa no encaja.
"""
import numpy as np
from manifold3d import Manifold, CrossSection, JoinType

# ---------------- PARÁMETROS (mm) ----------------
LARGO   = 85.0   # largo de la placa
ANCHO   = 50.0   # ancho de la placa
GROSOR  = 15.0   # de la cara de la pantalla a lo más alto de detrás
HOLGURA = 0.4    # aire por cada lado (si queda flojo: 0.2; si no entra: 0.6)
PARED   = 2.0    # grosor de las paredes
LABIO   = 1.5    # cuánto "muerde" el marco por encima del borde de la placa
T_LABIO = 1.6    # grosor del labio (lo que sobresale por delante y por detrás)
ESQUINA = 12.0   # largo de cada esquina protectora
RADIO   = 3.0    # redondeo exterior
NFC     = True   # puente por detrás para la pegatina NFC
NFC_D   = 20.0   # diámetro de la pegatina
NFC_PROF = 0.4   # profundidad del rebaje donde se encaja
# -------------------------------------------------

def rect(x, y, r=0.0):
    cs = CrossSection.square([x - 2 * r, y - 2 * r], center=True)
    return cs.offset(r, JoinType.Round, circular_segments=48) if r > 0 else cs

iL, iW = LARGO + 2 * HOLGURA, ANCHO + 2 * HOLGURA       # hueco interior
oL, oW = iL + 2 * PARED, iW + 2 * PARED                 # exterior
H = (GROSOR + HOLGURA) + 2 * T_LABIO                    # alto total

caja   = rect(oL, oW, RADIO).extrude(H)
hueco  = rect(iL, iW, 0.6).extrude(GROSOR + HOLGURA).translate([0, 0, T_LABIO])
ventana = rect(iL - 2 * LABIO, iW - 2 * LABIO, 2.0).extrude(H + 2).translate([0, 0, -1])
marco = caja - hueco - ventana

# Por detrás solo quedan las 4 esquinas: se quita todo lo que hay entre ellas
# (lados y labio trasero), así se ven los componentes y los puertos quedan libres.
z0 = T_LABIO                       # justo encima del labio delantero
alto = H - z0 + 1
largo_hueco = oL - 2 * ESQUINA
ancho_hueco = oW - 2 * ESQUINA
quitar = (Manifold.cube([largo_hueco, oW + 2, alto]).translate([-largo_hueco / 2, -oW / 2 - 1, z0])
        + Manifold.cube([oL + 2, ancho_hueco, alto]).translate([-oL / 2 - 1, -ancho_hueco / 2, z0]))
marco = marco - quitar

# Puente NFC: franja maciza por detrás en un extremo, con rebaje redondo.
# Las paredes largas de ese tramo quedan macizas para que el puente se apoye;
# el lado corto sigue abierto (puede haber un puerto ahí).
if NFC:
    banda = NFC_D + 4
    x0 = oL / 2 - banda
    zona = Manifold.cube([banda, oW + 2, H + 2]).translate([x0, -oW / 2 - 1, -1])
    ventana_frente = rect(iL - 2 * LABIO, iW - 2 * LABIO, 2.0).extrude(T_LABIO + 1).translate([0, 0, -1])
    tramo = (caja - hueco - ventana_frente) ^ zona
    tramo = tramo - Manifold.cube([PARED + 2, max(ancho_hueco, 0.1), H - 2 * T_LABIO]).translate(
        [oL / 2 - PARED - 1, -max(ancho_hueco, 0.1) / 2, T_LABIO])
    rebaje = Manifold.cylinder(NFC_PROF + 1, (NFC_D + 0.5) / 2, circular_segments=96).translate(
        [oL / 2 - banda / 2, 0, H - NFC_PROF])
    marco = (marco + tramo) - rebaje

# ---------------- exportar ----------------
mesh = marco.to_mesh()
v = np.asarray(mesh.vert_properties)[:, :3].astype(np.float32)
t = np.asarray(mesh.tri_verts)
tri = v[t]
n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
datos = np.zeros(len(t), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
datos["n"], datos["v"] = n, tri
with open("marco.stl", "wb") as f:
    f.write(b"marco ES3C28P".ljust(80, b" "))
    f.write(np.uint32(len(t)).tobytes())
    f.write(datos.tobytes())

print(f"Exterior: {oL:.1f} x {oW:.1f} x {H:.1f} mm  |  volumen {marco.volume()/1000:.1f} cm3  |  {len(t)} triángulos")

import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
fig = plt.figure(figsize=(12, 5))
for i, (el, az, tit) in enumerate([(35, -60, "Por delante (pantalla)"), (-35, -60, "Por detrás (componentes)")]):
    ax = fig.add_subplot(1, 2, i + 1, projection="3d")
    ax.add_collection3d(Poly3DCollection(tri, facecolor="#e8833a", edgecolor="#7a3d12", linewidths=0.05))
    ax.set_xlim(-oL/2, oL/2); ax.set_ylim(-oL/2, oL/2); ax.set_zlim(-oL/2 + H/2, oL/2 + H/2)
    ax.view_init(el, az); ax.set_axis_off(); ax.set_title(tit)
plt.tight_layout(); plt.savefig("marco.png", dpi=110)
