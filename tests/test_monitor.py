import importlib.util
import pathlib
import sys
import unittest


MODULE_PATH = pathlib.Path(__file__).parents[1] / "scripts" / "binance_long_monitor.py"
SPEC = importlib.util.spec_from_file_location("monitor", MODULE_PATH)
monitor = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = monitor
SPEC.loader.exec_module(monitor)


def rows(closes, spread=1.0, volume=1000.0):
    output = []
    for index, close in enumerate(closes):
        output.append([index, str(close), str(close + spread), str(close - spread), str(close), str(volume)])
    return output


class IndicatorTests(unittest.TestCase):
    def test_rsi_bounds_and_flat_up_series(self):
        self.assertEqual(monitor.rsi([float(value) for value in range(1, 30)]), 100.0)

    def test_vwap_uses_typical_price_and_volume(self):
        data = [[0, "1", "3", "1", "2", "10"], [1, "2", "6", "2", "4", "30"]]
        self.assertAlmostEqual(monitor.approximate_vwap(data), 3.5)

    def test_tradfi_exclusion_does_not_exclude_crypto_substrings(self):
        self.assertTrue(monitor.tradfi_symbol("ARMUSDT"))
        self.assertFalse(monitor.tradfi_symbol("FARMUSDT"))

    def test_successive_new_lows(self):
        falling = rows([20, 19, 18, 17, 16, 15, 14, 13, 12])
        recovering = rows([20, 18, 16, 14, 15, 16, 17, 18, 19])
        self.assertTrue(monitor.successive_new_lows(falling))
        self.assertFalse(monitor.successive_new_lows(recovering))


class CandidateTests(unittest.TestCase):
    def test_ready_candidate_has_defined_three_point_five_r_target(self):
        closes = [100 - index * 0.8 for index in range(35)] + [72 + index * 1.4 for index in range(25)]
        data = rows(closes, spread=0.5)
        ticker = {"highPrice": "200", "lowPrice": "60", "lastPrice": str(closes[-1])}
        candidate = monitor.evaluate("TESTUSDT", ticker, 0.0001, data)
        self.assertEqual(candidate.status, "ENTRY_READY")
        self.assertAlmostEqual((candidate.tp2 - candidate.trigger) / (candidate.trigger - candidate.stop), 3.5)

    def test_overheated_funding_prevents_entry(self):
        closes = [100 - index * 0.8 for index in range(35)] + [72 + index * 1.4 for index in range(25)]
        data = rows(closes, spread=0.5)
        ticker = {"highPrice": "200", "lowPrice": "60", "lastPrice": str(closes[-1])}
        candidate = monitor.evaluate("TESTUSDT", ticker, 0.002, data)
        self.assertNotEqual(candidate.status, "ENTRY_READY")
        self.assertIn("funding", candidate.reason)


if __name__ == "__main__":
    unittest.main()
