"""Sword styles drawn over the game's own sword frames (2026-11-25).

Colour numbers are the weapon palette's own, used as the native sword uses
them: 4 = dark outline, 1 = bright edge, 2 = blade, 3 = shade, 5 = slash trail.
The trail is kept exactly; the sword (colours 1..4) is taken out and a blade
is drawn at its hilt, along its axis. Frames where the sword points at the
viewer stay native."""
import math

from collections import namedtuple

BLADE = 15.0                       # the native sword's blade, px
# length (of the native blade), grip px, sag px (bend to the back), half width
# at the guard and at the tip, point px, guard half width, guard kind.
# edges 1: single-edged (bright edge, shaded back); 2: both edges bright
# around a shaded fuller. thin: a one-pixel blade.
Blade = namedtuple("Blade", "length grip sag base tip point guard kind edges")
STYLES = {
    "tanto":     Blade(0.42, 3, 0.2, 1.00, 1.00, 2.0, 2.0, "tsuba", 1),
    "katana":    Blade(1.15, 5, 1.6, 1.00, 1.00, 2.0, 2.0, "tsuba", 1),
    "dai":       Blade(1.75, 7, 3.0, 1.00, 1.00, 2.0, 2.0, "tsuba", 1),
    "rapier":    Blade(1.35, 3, 0.0, 0.55, 0.55, 1.0, 2.2, "cup", 2),
    "longsword": Blade(1.25, 5, 0.0, 1.55, 1.35, 3.0, 4.0, "cross", 2),
    "claymore":  Blade(1.75, 7, 0.0, 2.50, 1.90, 4.0, 5.0, "cross", 2),
    "gladius":   Blade(0.75, 3, 0.0, 1.50, 2.10, 4.0, 2.2, "cross", 2),
    "arming":    Blade(1.00, 3, 0.0, 1.55, 1.10, 3.0, 3.2, "cross", 2),
    "bastard":   Blade(1.45, 6, 0.0, 2.05, 1.40, 4.0, 4.0, "cross", 2),
    "dagger":    Blade(0.50, 3, 0.0, 1.10, 0.90, 3.0, 2.2, "cross", 2),
    "cutlass":   Blade(0.95, 3, 2.6, 1.10, 1.90, 3.5, 2.0, "basket", 1),
    "estoc":     Blade(1.50, 5, 0.0, 1.05, 0.50, 2.0, 3.0, "cross", 2),
}
TRAIL = 5


def islands(points):
    points, out = set(points), []
    while points:
        stack = [points.pop()]
        group = []
        while stack:
            x, y = stack.pop()
            group.append((x, y))
            for q in [(x+i, y+j) for i in (-1, 0, 1) for j in (-1, 0, 1)]:
                if q in points:
                    points.remove(q)
                    stack.append(q)
        out.append(group)
    return out


def hilt_of(image):
    """(guard centre, unit axis towards the tip) or None."""
    pts = [(x, y) for y, row in enumerate(image) for x, c in enumerate(row) if 1 <= c <= 4]
    if not pts:
        return None
    # equal islands: the one whose top-left pixel is lowest (som_art_gen.h too)
    group = max(islands(pts), key=lambda g: (len(g), min((y, x) for x, y in g)))
    n = len(group)
    if n < 20:
        return None
    mx, my = sum(p[0] for p in group)/n, sum(p[1] for p in group)/n
    sxx = sum((p[0]-mx)**2 for p in group)
    syy = sum((p[1]-my)**2 for p in group)
    sxy = sum((p[0]-mx)*(p[1]-my) for p in group)
    a = 0.5*math.atan2(2*sxy, sxx-syy)
    ux, uy = math.cos(a), math.sin(a)
    t = [(p[0]-mx)*ux+(p[1]-my)*uy for p in group]
    lo, hi = min(t), max(t)
    if hi-lo < 8:
        return None
    def spread(at):                # width across the axis
        v = [-(p[0]-mx)*uy+(p[1]-my)*ux for p, q in zip(group, t) if abs(q-at) <= 0.8]
        return (max(v)-min(v), sum(v)/len(v)) if v else (0, 0)
    steps = [lo+0.5*i for i in range(int((hi-lo)/0.5)+1)]
    at = max(steps, key=lambda s: (spread(s)[0], abs(s)))
    if spread(at)[0] < 4:          # no guard to be seen
        return None
    sign = -1.0 if at > (lo+hi)/2 else 1.0
    guard = (mx+ux*at-uy*spread(at)[1], my+uy*at+ux*spread(at)[1])
    blade = [p for p, q in zip(group, t) if (q-at)*sign > 2.5]
    if len(blade) < 8:
        return None
    bx, by = sum(p[0] for p in blade)/len(blade)-guard[0], sum(p[1] for p in blade)/len(blade)-guard[1]
    d = math.hypot(bx, by)
    return guard, (bx/d, by/d)


def draw(image, style):
    """The frame with the style's blade, or None for the native sword."""
    found = hilt_of(image)
    if not found:
        return None
    (gx, gy), (ux, uy) = found
    height, width = len(image), len(image[0])
    trail = [(x, y) for y, row in enumerate(image) for x, c in enumerate(row) if c == TRAIL]
    # The edge leads the swing, so the back faces the trail.
    side = 1.0 if ux > 0 or (ux == 0 and uy < 0) else -1.0      # edge down in game view
    if trail:
        tx, ty = sum(p[0] for p in trail)/len(trail)-gx, sum(p[1] for p in trail)/len(trail)-gy
        cross = -tx*uy+ty*ux
        if abs(cross) > 1:
            side = -1.0 if cross > 0 else 1.0
    b = STYLES[style]
    length = BLADE*b.length
    out = [[c if c == TRAIL else 0 for c in row] for row in image]
    core = {}
    reach = int(length+b.sag+b.grip+8)
    for y in range(max(0, int(gy)-reach), min(height, int(gy)+reach+1)):
        for x in range(max(0, int(gx)-reach), min(width, int(gx)+reach+1)):
            dx, dy = x-gx, y-gy
            t, v = dx*ux+dy*uy, (-dx*uy+dy*ux)*side             # v > 0: the edge
            v += b.sag*(max(0.0, t)/length)**2                  # bends to the back
            thick = 1.4 if b.kind == "cup" else 0.75
            if abs(t) <= thick and abs(v) <= b.guard:           # guard
                core[x, y] = 3 if abs(v) > b.guard-0.9 else 2
            elif b.kind == "basket" and -b.grip-0.5 <= t < -0.5 and 1.7 <= v <= 2.6:
                core[x, y] = 2                                  # knuckle bow
            elif -b.grip <= t < 0 and abs(v) <= 0.75:           # wrapped grip
                core[x, y] = 3 if int(-t) % 3 == 1 else 4
            elif b.kind == "cross" and b.grip > 4 and abs(t+b.grip+1) <= 1.1 and abs(v) <= 1.2:
                core[x, y] = 2                                  # pommel
            elif 0 < t <= length:
                left = length-t
                half = b.base+(b.tip-b.base)*t/length
                if b.edges == 1:
                    if not (-0.95 <= v <= half+0.05):
                        continue
                    if left < b.point and v > left-0.6:         # cut from the edge
                        continue
                    core[x, y] = 1 if v > half-1.0 else 2 if t < length*0.55 else 3
                else:
                    if left < b.point:
                        half *= max(0.35, left/b.point)
                    if abs(v) > half:
                        continue
                    core[x, y] = (1 if half < 0.8 or v > 0.45 else
                                  2 if v < -0.45 or half < 1.3 else 3)
    if not core:
        return None
    edge = {(x+i, y+j) for x, y in core for i, j in ((1, 0), (-1, 0), (0, 1), (0, -1))}-set(core)
    for x, y in edge:
        if 0 <= x < width and 0 <= y < height:
            out[y][x] = 4
    for (x, y), c in core.items():
        if 0 <= x < width and 0 <= y < height:
            out[y][x] = c
    return out
