from __future__ import annotations

import math
from pathlib import Path


OUT = Path(__file__).resolve().parent


def normal(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1
    return nx / length, ny / length, nz / length


class Mesh:
    def __init__(self, name: str):
        self.name = name
        self.tris = []

    def tri(self, a, b, c):
        self.tris.append((a, b, c))

    def box(self, x, y, z, sx, sy, sz):
        p = [
            (x, y, z),
            (x + sx, y, z),
            (x + sx, y + sy, z),
            (x, y + sy, z),
            (x, y, z + sz),
            (x + sx, y, z + sz),
            (x + sx, y + sy, z + sz),
            (x, y + sy, z + sz),
        ]
        faces = [
            (0, 2, 1), (0, 3, 2),
            (4, 5, 6), (4, 6, 7),
            (0, 1, 5), (0, 5, 4),
            (1, 2, 6), (1, 6, 5),
            (2, 3, 7), (2, 7, 6),
            (3, 0, 4), (3, 4, 7),
        ]
        for a, b, c in faces:
            self.tri(p[a], p[b], p[c])

    def cylinder(self, cx, cy, z, radius, height, segments=48):
        bottom = []
        top = []
        for i in range(segments):
            angle = 2 * math.pi * i / segments
            bottom.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle), z))
            top.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle), z + height))
        cb = (cx, cy, z)
        ct = (cx, cy, z + height)
        for i in range(segments):
            j = (i + 1) % segments
            self.tri(cb, bottom[j], bottom[i])
            self.tri(ct, top[i], top[j])
            self.tri(bottom[i], bottom[j], top[j])
            self.tri(bottom[i], top[j], top[i])

    def ring(self, cx, cy, z, outer, inner, height, segments=48):
        ob, ot, ib, it = [], [], [], []
        for i in range(segments):
            angle = 2 * math.pi * i / segments
            co, si = math.cos(angle), math.sin(angle)
            ob.append((cx + outer * co, cy + outer * si, z))
            ot.append((cx + outer * co, cy + outer * si, z + height))
            ib.append((cx + inner * co, cy + inner * si, z))
            it.append((cx + inner * co, cy + inner * si, z + height))
        for i in range(segments):
            j = (i + 1) % segments
            self.tri(ob[i], ob[j], ot[j])
            self.tri(ob[i], ot[j], ot[i])
            self.tri(ib[j], ib[i], it[i])
            self.tri(ib[j], it[i], it[j])
            self.tri(ot[i], ot[j], it[j])
            self.tri(ot[i], it[j], it[i])
            self.tri(ob[j], ob[i], ib[i])
            self.tri(ob[j], ib[i], ib[j])

    def write(self, path: Path):
        with path.open("w", encoding="ascii") as f:
            f.write(f"solid {self.name}\n")
            for a, b, c in self.tris:
                nx, ny, nz = normal(a, b, c)
                f.write(f"  facet normal {nx:.6g} {ny:.6g} {nz:.6g}\n")
                f.write("    outer loop\n")
                for p in (a, b, c):
                    f.write(f"      vertex {p[0]:.6g} {p[1]:.6g} {p[2]:.6g}\n")
                f.write("    endloop\n")
                f.write("  endfacet\n")
            f.write(f"endsolid {self.name}\n")


def make_bottom():
    m = Mesh("esp32_dongle_case_bottom")
    length, width, height = 78, 42, 16
    wall, floor = 2, 2

    m.box(0, 0, 0, length, width, floor)
    m.box(2, 0, floor, length - 4, wall, height - floor)
    m.box(2, width - wall, floor, length - 4, wall, height - floor)
    m.box(length - wall, 0, floor, wall, width, height - floor)

    # USB end with centered cable/connector cutout: y 14..28, z 2..11.
    m.box(0, 0, floor, wall, 14, height - floor)
    m.box(0, 28, floor, wall, 14, height - floor)
    m.box(0, 14, 11, wall, 14, height - 11)

    # Board support rails and adhesive/standoff pads.
    m.box(10, 8, floor, 56, 2, 2)
    m.box(10, 32, floor, 56, 2, 2)
    for x in (17, 61):
        for y in (10.5, 31.5):
            m.cylinder(x, y, floor, 2.6, 3.0, 32)

    # Cable strain-relief saddle near USB end.
    m.box(4, 12, floor, 5, 18, 3)
    return m


def lid_plate_with_grille(m: Mesh):
    length, width = 78, 42
    z, t = 0, 2
    # Build plate in strips, leaving five acoustic slots near rear/top mic area.
    slots = [(54, 14 + i * 3.2, 14, 1.4) for i in range(5)]
    y_breaks = sorted({0, width, *[s[1] for s in slots], *[s[1] + s[3] for s in slots]})
    for y0, y1 in zip(y_breaks, y_breaks[1:]):
        covering = [s for s in slots if abs(s[1] - y0) < 1e-6 and abs(s[1] + s[3] - y1) < 1e-6]
        if covering:
            sx, sy, sw, sh = covering[0]
            m.box(0, y0, z, sx, y1 - y0, t)
            m.box(sx + sw, y0, z, length - sx - sw, y1 - y0, t)
        else:
            m.box(0, y0, z, length, y1 - y0, t)


def make_lid():
    m = Mesh("esp32_dongle_case_lid")
    lid_plate_with_grille(m)
    # Inner lip that drops into the tray.
    m.box(3, 3, -3, 72, 2, 3)
    m.box(3, 37, -3, 72, 2, 3)
    m.box(74, 3, -3, 2, 36, 3)
    m.box(3, 3, -3, 2, 10, 3)
    m.box(3, 29, -3, 2, 10, 3)
    # Raised mic label pad/guard around the grille.
    m.box(52, 11, 2, 18, 20, 0.8)
    return m


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    bottom = make_bottom()
    lid = make_lid()
    bottom.write(OUT / "esp32_dongle_case_bottom.stl")
    lid.write(OUT / "esp32_dongle_case_lid.stl")
    print(OUT / "esp32_dongle_case_bottom.stl")
    print(OUT / "esp32_dongle_case_lid.stl")


if __name__ == "__main__":
    main()
