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
    def test_cases_alternate_ab_ba_without_changing_pair_members(self):
        builds = [('baseline', Path('a')), ('candidate', Path('b'))]
        forward = benchmark.run_order(builds, [0, 1], 0)
        self.assertEqual(forward, [('baseline', Path('a'), 0), ('baseline', Path('a'), 1),
                                   ('candidate', Path('b'), 0), ('candidate', Path('b'), 1)])
        self.assertEqual(benchmark.run_order(builds, [0, 1], 1), forward[::-1])
        self.assertEqual(benchmark.run_order(builds, [0, 1], 2), forward)

    def test_summary_excludes_warmups_and_keeps_run_variation(self):
        results = [dict(warmup=warmup, build='candidate', hd=1, mean_ms={'compose': cost})
                   for warmup, cost in [(True, 100), (False, 2), (False, 4), (False, 6)]]
        summary, = benchmark.summarize_runs(results)
        self.assertEqual(summary['runs'], 3)
        stats = summary['mean_ms']['compose']
        self.assertEqual(stats['median'], 4)
        self.assertEqual((stats['minimum'], stats['maximum']), (2, 6))
        self.assertEqual((stats['p25'], stats['p75']), (2, 6))

    def test_paired_comparison_reports_median_delta_and_overlap(self):
        results = [
            dict(warmup=False, build='baseline', hd=1, mean_ms={'compose': 4.0, 'raster-capture': 2.0}),
            dict(warmup=False, build='candidate', hd=1, mean_ms={'compose': 3.5, 'raster-capture': 2.1}),
            dict(warmup=False, build='candidate', hd=1, mean_ms={'compose': 3.0, 'raster-capture': 2.2}),
            dict(warmup=False, build='baseline', hd=1, mean_ms={'compose': 3.2, 'raster-capture': 1.9}),
            dict(warmup=True, build='baseline', hd=1, mean_ms={'compose': 100.0, 'raster-capture': 20.0}),
        ]
        comparison = benchmark.paired_comparison(results)
        compose = comparison['hd1']['mean_ms']['compose']
        self.assertAlmostEqual(compose['baseline_median'], 3.6)
        self.assertAlmostEqual(compose['candidate_median'], 3.25)
        self.assertAlmostEqual(compose['delta_percent'], (3.25 - 3.6) / 3.6 * 100)
        self.assertTrue(compose['ranges_overlap'])
        self.assertIn('inconclusive', compose['assessment'])

    def test_diagnostic_windows_separate_entry_from_warm_draws(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'cost.csv'
            path.write_text('frame,orbit_prepare_ms,orbit_parallel_ms\n'
                            '500,0,0\n501,20,3\n502,0.1,2\n503,0.2,2\n')
            entry = benchmark.cost_timings(path, 501, 501)
            warm = benchmark.cost_timings(path, 502, 503)
        self.assertEqual(entry['orbit_prepare_ms']['mean'], 20)
        self.assertAlmostEqual(warm['orbit_prepare_ms']['mean'], .15)
        self.assertEqual(warm['orbit_parallel_ms']['count'], 2)

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
        self.assertEqual(result['distributions']['renderer_ms']['max'], 53)
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
