import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from perf_cursor import sweep_point,absolute_coordinate,CursorSweep


class CursorTests(unittest.TestCase):
    def test_movement_hold_reverse_and_negative_monitor_origin(self):
        rect=(-1900,100,-1100,700)
        points=[sweep_point(t,rect) for t in (0,.5,1,1.5,2,2.5,3,3.5,4)]
        self.assertLess(points[0][0],points[1][0]); self.assertLess(points[1][0],points[2][0])
        self.assertEqual(points[2],points[3]); self.assertEqual(points[2],points[4])
        self.assertEqual(points[1],points[5]); self.assertEqual(points[0],points[6])
        self.assertEqual(points[0],points[7]); self.assertEqual(points[0],points[8])
        self.assertTrue(all(rect[0]<x<rect[2] and rect[1]<y<rect[3] for x,y in points))
    def test_absolute_mapping_targets_pixel_centers(self):
        for origin,extent in ((-1920,5760),(0,3840),(0,1)):
            for value in (origin,origin+extent//2,origin+extent-1):
                normalized=absolute_coordinate(value,origin,extent)
                self.assertEqual(origin+normalized*extent//65536,value)
        with self.assertRaises(ValueError): absolute_coordinate(-1,0,3840)
        with self.assertRaises(ValueError): absolute_coordinate(3840,0,3840)
    def test_focus_loss_and_button_press_prevent_injection(self):
        sweep=object.__new__(CursorSweep)
        sweep.foreground=lambda:False
        sweep.move=lambda _:self.fail('must not inject')
        with self.assertRaisesRegex(RuntimeError,'foreground'): sweep.step(0)
        sweep.foreground=lambda:True; sweep.buttons_down=lambda:True
        with self.assertRaisesRegex(RuntimeError,'button'): sweep.step(0)
    def test_manual_movement_stops_injection_and_prevents_restore(self):
        sweep=object.__new__(CursorSweep)
        sweep.foreground=lambda:True; sweep.buttons_down=lambda:False
        sweep.rect=(0,0,800,600); sweep.client_rect=lambda:sweep.rect
        sweep.last=(200,240); sweep.position=lambda:(300,280)
        sweep.move=lambda _:self.fail('must not override manual movement')
        with self.assertRaisesRegex(RuntimeError,'expected .*actual'): sweep.step(.1)
        sweep.restore()
    def test_geometry_change_stops_injection(self):
        sweep=object.__new__(CursorSweep)
        sweep.foreground=lambda:True; sweep.buttons_down=lambda:False
        sweep.rect=(0,0,800,600); sweep.client_rect=lambda:(10,0,810,600)
        sweep.move=lambda _:self.fail('must not inject after window movement')
        with self.assertRaisesRegex(RuntimeError,'geometry'): sweep.step(0)
