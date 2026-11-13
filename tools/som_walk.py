"""Bounded native walking against a current using tiny fresh safety reads."""
import time


class WalkFailed(Exception):
    pass


class CurrentWalk:
    def __init__(self, link, action, clock=time.monotonic, sleep=time.sleep, origin=None, waypoints=None):
        if action not in ("up", "right","left","down"):
            raise ValueError("only verified opposing-current directions")
        self.link,self.action,self.clock,self.sleep = link,action,clock,sleep
        self.origin = origin
        self.waypoints = waypoints

    def run(self):
        if self.waypoints:
            return self._follow_corridor()
        before = self.link.fetch_navigation()
        expected = 0x31 if self.action=="up" else 0x32
        if before is None or not before["valid"]:
            raise WalkFailed("initial current state is unverified")
        if self.origin is None:
            if before["ground"] != expected:
                raise WalkFailed("initial current state is unverified")
        elif (before["map"]!=self.origin[0] or
                abs(before["x"]-self.origin[1])+abs(before["y"]-self.origin[2]) > 16 or
                before["ground"] not in (0,expected)):
            raise WalkFailed("current moved outside the verified corridor")
        start = self.clock()
        last_motion = last_fresh = last_send = start
        previous = before
        best = 0
        self.link.send_action(self.action)
        try:
            while self.clock()-start < 1.8:
                self.sleep(.06)
                after = self.link.fetch_navigation()
                now = self.clock()
                if (after is None or not after["valid"] or after["map"]!=before["map"] or
                        after["layer"]!=before["layer"] or after["hp"]<before["hp"] or
                        after["max_hp"]!=before["max_hp"]):
                    raise WalkFailed("takeover, map/menu, binding, damage or status change")
                if after["tick"]==previous["tick"]:
                    if now-last_fresh > .4:
                        raise WalkFailed("stale navigation snapshot")
                    continue
                last_fresh = now
                progress = before["y"]-after["y"] if self.action=="up" else after["x"]-before["x"]
                sideways = abs(after["x"]-before["x"]) if self.action=="up" else abs(after["y"]-before["y"])
                if sideways > 12 or progress < -4:
                    raise WalkFailed("unexpected current displacement")
                if progress > best:
                    best,last_motion = progress,now
                if best >= 12 and after["ground"]!=expected:
                    return best
                if best >= 64:
                    raise WalkFailed("verified corridor limit reached")
                if now-last_motion > .35:
                    raise WalkFailed("no verified upstream progress")
                if progress > (before["y"]-previous["y"] if self.action=="up" else previous["x"]-before["x"]) and now-last_send >= .15:
                    self.link.send_action(self.action)
                    last_send = now
                previous = after
            raise WalkFailed("upstream walk timeout")
        finally:
            self.link.send_action("wait")

    def _follow_corridor(self):
        before = self.link.fetch_navigation()
        if (before is None or not before["valid"] or self.origin is None or
                before["map"]!=self.origin[0] or
                abs(before["x"]-self.origin[1])+abs(before["y"]-self.origin[2]) > 20):
            raise WalkFailed("initial corridor state changed")
        start = last_fresh = last_motion = self.clock()
        previous = before
        last_action = None
        last_send = float("-inf")
        index = 0
        try:
            while self.clock()-start < 1.8:
                after = self.link.fetch_navigation()
                now = self.clock()
                if (after is None or not after["valid"] or after["map"]!=before["map"] or
                        after["layer"]!=before["layer"] or after["hp"]<before["hp"] or
                        after["max_hp"]!=before["max_hp"]):
                    raise WalkFailed("corridor safety or takeover changed")
                if after["tick"]==previous["tick"] and last_action is not None:
                    if now-last_fresh > .4:raise WalkFailed("stale corridor snapshot")
                    self.sleep(.04);continue
                last_fresh = now
                if max(abs(after["x"]-self.origin[1]),abs(after["y"]-self.origin[2])) > 56:
                    raise WalkFailed("corridor bounds exceeded")
                along = None
                if last_action is not None:
                    dx,dy=after["x"]-previous["x"],after["y"]-previous["y"]
                    along={"right":dx,"left":-dx,"down":dy,"up":-dy}[last_action]
                    if along > 0:last_motion=now
                    if now-last_motion > .35:raise WalkFailed("no corridor progress")
                tx,ty=self.waypoints[index][0]*16+8,self.waypoints[index][1]*16+8
                dx,dy=tx-after["x"],ty-after["y"]
                sx,sy=(before["x"],before["y"]) if index==0 else (self.waypoints[index-1][0]*16+8,self.waypoints[index-1][1]*16+8)
                vx,vy=tx-sx,ty-sy
                if abs(vx)>abs(vy):
                    passed=(after["x"]>=tx if vx>0 else after["x"]<=tx) and abs(dy)<=8
                else:
                    passed=(after["y"]>=ty if vy>0 else after["y"]<=ty) and abs(dx)<=8
                # A sampled position can skip a four-pixel target window.
                # Crossing the waypoint advances the route; never reverse
                # into the current just to hit an exact pixel afterwards.
                if passed or max(abs(dx),abs(dy)) <= 4:
                    index+=1
                    if index==len(self.waypoints):
                        return abs(after["x"]-before["x"])+abs(after["y"]-before["y"])
                    tx,ty=self.waypoints[index][0]*16+8,self.waypoints[index][1]*16+8
                    dx,dy=tx-after["x"],ty-after["y"]
                action=("right" if dx>0 else "left") if abs(dx)>abs(dy) else ("down" if dy>0 else "up")
                if action!=last_action or (now-last_send >= .15 and along is not None and along > 0):
                    self.link.send_action(action)
                    last_action,last_send=action,now
                previous=after
                self.sleep(.06)
            raise WalkFailed("corridor timeout")
        finally:
            self.link.send_action("wait")
