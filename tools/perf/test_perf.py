"""Testele analizei.  python3 -m unittest -v test_perf  (din tools/perf)

Un jurnal scris de mână, cum l-ar scrie firmware-ul: un front de buton, o
apăsare ÎNAINTE, paginarea, o reîmprospătare parțială, apoi întoarceri
automate cu bateria în scădere.
"""
import contextlib
import io
import pathlib
import tempfile
import unittest

import perf

ROWS = [
    # Frontul se scrie după ce butonul ajunge la interfață, cu timpul lui.
    (1_000_000, "perf_open", 1, 0),
    (2_050_000, "key", 1, 0),
    (2_000_000, "btn_edge", 1, 0),
    (2_051_000, "layout_begin", 10, 0),
    (2_131_000, "layout_end", 22, 0),
    (2_741_000, "refresh", 0, 600_000),          # a început la 2 141 000
    (2_741_000, "refresh_io", 40_000, 550_000),
    (2_741_000, "refresh_rect", 8 << 16 | 18, 224 << 16 | 391),
    (2_745_000, "perf_flush", 9, 3_000),
    # O tastă în meniu: fără paginare, nu e o întoarcere.
    (3_000_000, "key", 2, 0),
    (3_400_000, "refresh", 0, 300_000),
    (3_400_000, "refresh_io", 20_000, 270_000),
    (3_400_000, "refresh_rect", 0, 240 << 16 | 413),
    (4_000_000, "auto_set", 60_000, 0),
    (64_000_000, "auto_turn", 30, 4100),
    (64_000_100, "layout_begin", 30, 0),
    (64_090_100, "layout_end", 22, 0),
    (66_000_000, "refresh", 1, 1_900_000),
    (66_000_000, "refresh_io", 40_000, 1_850_000),
    (66_000_000, "refresh_rect", 0, 240 << 16 | 416),
    (70_000_000, "battery", 4050, 0),
    (124_000_000, "auto_turn", 31, 4000),
    (124_000_100, "layout_begin", 31, 0),
    (124_090_100, "layout_end", 21, 0),
    (124_700_000, "refresh", 0, 600_000),
    (124_700_000, "refresh_io", 40_000, 550_000),
    (124_990_000, "light_sleep", 100_000, 412),
    (125_000_000, "sleep", 0, 0),
]


def write(rows, path):
    path.write_text("t_us,event,a,b\n" + "".join(f"{t},{e},{a},{b}\n" for t, e, a, b in rows))


class Analysis(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = pathlib.Path(self.dir.name) / "0001.csv"
        write(ROWS, self.path)
        self.log = perf.read(self.path)

    def tearDown(self):
        self.dir.cleanup()

    def test_turns_and_parts(self):
        ts = perf.turns(self.log)
        self.assertEqual([t.trigger for t in ts], ["ÎNAINTE", "auto", "auto"])
        p = ts[0].parts()
        self.assertAlmostEqual(p["intrare"], 50.0)
        self.assertAlmostEqual(p["pregătire"], 1.0)
        self.assertAlmostEqual(p["paginare"], 80.0)
        self.assertAlmostEqual(p["desenare"], 10.0)
        self.assertAlmostEqual(p["spi"], 40.0)
        self.assertAlmostEqual(p["panou"], 550.0)
        self.assertAlmostEqual(p["total"], 741.0)
        self.assertNotIn("intrare", ts[1].parts())
        self.assertTrue(ts[1].refresh.full)
        self.assertEqual(ts[0].refresh.rect, (8, 18, 224, 391))

    def test_edge_must_match_the_button(self):
        rows = [r if r[1] != "btn_edge" else (r[0], r[1], 0, 0) for r in ROWS]   # ÎNAPOI, nu ÎNAINTE
        write(rows, self.path)
        self.assertIsNone(perf.turns(perf.read(self.path))[0].edge)

    def test_battery_curve(self):
        c = perf.battery_curve(self.log)
        self.assertEqual([mv for _, mv, _ in c], [4100, 4050, 4000])
        self.assertEqual(c[-1][2], 2)
        self.assertAlmostEqual(c[-1][0], 60 / 3600)

    def test_truncated_last_row_is_skipped(self):
        with open(self.path, "a") as f:
            f.write("125000500,refr")
        self.assertEqual(len(perf.read(self.path).events), len(ROWS))

    def test_report_and_csv(self):
        out = pathlib.Path(self.dir.name)
        with contextlib.redirect_stdout(io.StringIO()) as s:
            perf.main([str(self.path), "--pagini", str(out / "p.csv"), "--baterie", str(out / "b.csv")])
        self.assertIn("toate: 3 (auto 2, ÎNAINTE 1)", s.getvalue())
        self.assertIn("2 pagini în 0.02 h, 4100 → 4000 mV; adormit", s.getvalue())
        self.assertIn("100.0 s din 125.0 s (80%), 412 adormiri", s.getvalue())
        self.assertEqual(len((out / "p.csv").read_text().splitlines()), 4)
        self.assertEqual(len((out / "b.csv").read_text().splitlines()), 4)

    def test_not_a_log(self):
        self.path.write_text("PERF,refresh\n")
        with self.assertRaises(SystemExit):
            perf.read(self.path)


if __name__ == "__main__":
    unittest.main()
