import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    'benchmark_hd', Path(__file__).resolve().parents[1] / 'tools/benchmark_hd.py')
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class HdBenchmarkTest(unittest.TestCase):
    def test_tail_and_cache_clear_are_attributed_to_the_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'timing.csv'
            with path.open('w', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(['frame', 'present_seconds', 'guest_periods',
                                 'guest_ms', 'raster_ms', 'wait_ms',
                                 'hd_cache_clears', 'hd_palette_changes', 'hd_cached_tiles'])
                writer.writerows([[350, .010, 1, 2, 3, 5, 1, 0, 100],
                                  [351, .020, 1, 2, 3, 5, 1, 0, 100],
                                  [352, .080, 1, 2, 53, 5, 2, 7, 10]])
            result = benchmark.frame_timings(path)
        interval = result['distributions']['interval_ms']
        self.assertEqual(interval['count'], 2)
        self.assertAlmostEqual(interval['p50'], 10)
        self.assertAlmostEqual(interval['p99'], 60)
        self.assertEqual(interval['max_frame'], 352)
        self.assertEqual(interval['over_budget']['50'], 1)
        self.assertEqual(result['distributions']['work_ms']['max'], 55)
        self.assertEqual(result['hd_counters']['hd_cache_clears'], 1)
        self.assertEqual(result['cache_pressure_events'][0]['frame'], 352)
        self.assertEqual(result['cache_pressure_events'][0]['raster_ms'], 53)
        self.assertIsNone(result['cache_pressure_events'][0]['following_raster_ms'])

    def test_old_trace_without_hd_counters_is_supported(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'timing.csv'
            path.write_text('frame,present_seconds,raster_ms\n350,0.01,2\n351,0.02,2\n')
            result = benchmark.frame_timings(path)
        self.assertEqual(result['hd_counters'], {})
        self.assertEqual(result['cache_pressure_events'], [])

    def test_missing_trace_samples_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'timing.csv'
            path.write_text('frame,present_seconds,raster_ms\n350,0.01,2\n')
            with self.assertRaisesRegex(AssertionError, 'missing per-presentation'):
                benchmark.frame_timings(path)

    def test_trim_includes_the_following_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'timing.csv'
            path.write_text('frame,present_seconds,raster_ms,hd_cache_trims\n'
                            '1,0.01,2,0\n2,0.03,12,1\n3,0.04,2,1\n')
            result = benchmark.frame_timings(path)
        self.assertEqual(result['hd_counters']['hd_cache_trims'], 1)
        event, = result['cache_pressure_events']
        self.assertEqual(event['frame'], 2)
        self.assertEqual(event['following_raster_ms'], 2)
