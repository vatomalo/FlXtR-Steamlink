"""Current walking renews inputs only after fresh observed progress."""
import unittest
from som_walk import CurrentWalk, WalkFailed


def frame(tick=1, y=100, **changes):
    return dict({"tick":tick,"map":128,"x":536,"y":y,"hp":98,"max_hp":98,
                 "layer":0,"ground":0x31,"valid":True},**changes)


class Link:
    def __init__(self, frames):
        self.frames=list(frames)
        self.sent=[]
    def fetch_navigation(self):
        return self.frames.pop(0) if len(self.frames)>1 else self.frames[0]
    def send_action(self, action):
        self.sent.append(action)


class TestCurrentWalk(unittest.TestCase):
    def setUp(self):
        self.now=0
    def sleep(self,n):
        self.now+=n
    def walk(self,frames):
        link=Link(frames)
        return link,CurrentWalk(link,"up",clock=lambda:self.now,sleep=self.sleep)
    def test_reaches_land_then_releases(self):
        link,walk=self.walk([frame(),frame(2,80),frame(3,60,ground=0)])
        self.assertEqual(walk.run(),40)
        self.assertEqual(link.sent,["up","wait"])
    def test_no_progress_does_not_repeat_blind_inputs(self):
        link,walk=self.walk([frame(tick=i) for i in range(10)])
        with self.assertRaisesRegex(WalkFailed,"progress"):
            walk.run()
        self.assertEqual(link.sent,["up","wait"])
    def test_stale_snapshots_abort(self):
        link,walk=self.walk([frame()])
        with self.assertRaisesRegex(WalkFailed,"stale"):
            walk.run()
        self.assertEqual(link.sent[-1],"wait")
    def test_takeover_damage_and_map_changes_abort(self):
        for after in (None,frame(2,80,hp=90),frame(2,80,map=129),frame(2,80,valid=False)):
            link,walk=self.walk([frame(),after])
            with self.assertRaises(WalkFailed):
                walk.run()
            self.assertEqual(link.sent[-1],"wait")
    def test_unknown_initial_current_has_no_inputs(self):
        link,walk=self.walk([frame(ground=0x33)])
        with self.assertRaises(WalkFailed):walk.run()
        self.assertEqual(link.sent,[])
    def test_corridor_can_steer_at_a_corner_using_fresh_small_reads(self):
        link=Link([frame(1,104,x=536),frame(2,104,x=536),frame(3,88,x=536),
                   frame(4,88,x=552)])
        walk=CurrentWalk(link,"up",clock=lambda:self.now,sleep=self.sleep,
                         origin=(128,536,104),waypoints=((33,5),(34,5)))
        self.assertEqual(walk.run(),32)
        self.assertEqual(link.sent,["up","right","wait"])
    def test_overshot_waypoint_is_acknowledged_without_reversing(self):
        link=Link([frame(1,104,x=536),frame(2,104,x=536),frame(3,104,x=559),
                   frame(4,104,x=575)])
        walk=CurrentWalk(link,"right",clock=lambda:self.now,sleep=self.sleep,
                         origin=(128,536,104),waypoints=((34,6),(35,6)))
        self.assertEqual(walk.run(),39)
        self.assertNotIn("left",link.sent)
    def test_corridor_does_not_renew_an_action_without_progress(self):
        link=Link([frame(i,100,x=536) for i in range(12)])
        walk=CurrentWalk(link,"up",clock=lambda:self.now,sleep=self.sleep,
                         origin=(128,536,100),waypoints=((33,5),))
        with self.assertRaisesRegex(WalkFailed,"progress"):walk.run()
        self.assertEqual(link.sent,["up","wait"])
