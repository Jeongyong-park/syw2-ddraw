import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('timing', Path(__file__).resolve().parents[1] / 'tools/sample_syw2_timing.py')
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


def rows(ticks, step=0.01):
    return [dict(time_s=i * step, ticks=value, state=3, speed_option=2,
                 base_ms=50, width=800, height=600, foreground=True, read_ms=0.1)
            for i, value in enumerate(ticks)]


class TimingTests(unittest.TestCase):
    def test_observed_flat_span_and_burst_are_not_exact_tick_times(self):
        r = timing.summarize(rows([10, 10, 10, 12]), 10, True)
        self.assertTrue(r['valid_for_comparison'])
        self.assertAlmostEqual(r['max_observed_unchanged_ticks_ms'], 20)
        self.assertEqual(r['multi_tick_observations'], 1)
        self.assertEqual(r['max_ticks_per_observation'], 2)

    def test_wrap_is_valid_but_reset_is_not(self):
        self.assertTrue(timing.summarize(rows([0xffffffff, 0]), 10, True)['valid_for_comparison'])
        r = timing.summarize(rows([100, 0, 1]), 10, True)
        self.assertFalse(r['valid_for_comparison'])
        self.assertIsNone(r['ticks_per_second'])

    def test_pause_focus_scene_and_sampling_gaps_invalidate_comparison(self):
        self.assertFalse(timing.summarize(rows([1, 2]), 10)['valid_for_comparison'])
        self.assertFalse(timing.summarize(rows([1, 1]), 10, True)['valid_for_comparison'])
        for key, value in [('foreground', False), ('state', 4), ('speed_option', 1),
                           ('width', 1068), ('read_ms', 20)]:
            sample = rows([1, 2]); sample[1][key] = value
            self.assertFalse(timing.summarize(sample, 10, True)['valid_for_comparison'], key)
        self.assertFalse(timing.summarize(rows([1, 2], 0.2), 10, True)['valid_for_comparison'])

    def test_bad_clock_and_unknown_image_fail_closed(self):
        with self.assertRaises(ValueError):
            timing.summarize(rows([1, 2], 0), 10, True)
        with self.assertRaises(ValueError):
            timing.image_digest(b'not a supported game')


if __name__ == '__main__':
    unittest.main()
