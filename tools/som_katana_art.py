"""Katana family drawn over the game's own sword frames (2026-11-25).

Colour numbers are the weapon palette's own, used as the native sword uses
them: 4 = dark outline, 1 = bright edge, 2 = blade, 3 = shade, 5 = slash trail.
The trail is kept exactly; the sword (colours 1..4) is taken out and a blade
is drawn at its hilt, along its axis. Frames where the sword points at the
viewer stay native."""
import math

BLADE = 15.0                       # the native sword's blade, px
LENGTH = {"tanto": 0.42, "katana": 1.15, "dai": 1.75}
GRIP = {"tanto": 3.0, "katana": 5.0, "dai": 7.0}
SAG = {"tanto": 0.2, "katana": 1.6, "dai": 3.0}
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
    group = max(islands(pts), key=len)
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
    length, grip, sag = BLADE*LENGTH[style], GRIP[style], SAG[style]
    out = [[c if c == TRAIL else 0 for c in row] for row in image]
    core = {}
    def local(x, y):
        dx, dy = x-gx, y-gy
        t, v = dx*ux+dy*uy, (-dx*uy+dy*ux)*side                 # v > 0: the edge
        bow = sag*(max(0.0, t)/length)**2 if length else 0.0
        return t, v+bow                                         # bends to the back
    reach = int(length+sag+grip+4)
    for y in range(max(0, int(gy)-reach), min(height, int(gy)+reach+1)):
        for x in range(max(0, int(gx)-reach), min(width, int(gx)+reach+1)):
            t, v = local(x, y)
            if abs(t) <= 0.75 and abs(v) <= 2.0:                # tsuba
                core[x, y] = 3 if abs(v) > 1.1 else 2
            elif -grip <= t < -0.75 and abs(v) <= 0.75:         # wrapped grip
                core[x, y] = 3 if int(-t) % 3 == 1 else 4
            elif 0.75 < t <= length and -0.95 <= v <= 1.05:
                left = length-t
                if left < 2.0 and v > left-0.6:                 # the point, cut from the edge
                    continue
                core[x, y] = 1 if v > 0.0 else 2 if t < length*0.55 else 3
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
