"""Testele analizei.  python3 -m unittest -v test_ina226  (din tools/ina226)

Capturi sintetice, scrise cum le scrie înregistratorul: un curent de repaus,
întoarceri de pagină la momente cunoscute, cu un decalaj de ceas cunoscut față
de jurnalul cititorului. Se verifică conversia, energia și alinierea.
"""
import csv
import io
import os
import pathlib
import tempfile
import unittest
from contextlib import redirect_stdout

import ina226

RATE = 850
DT_US = round(1e6 / RATE)
R_MOHM = 100
BUS_RAW = 3000          # 3000 × 1,25 mV = 3750 mV
HEADER = ("# ina226,v=1,target=esp32,addr=0x40,shunt_mohm=100,ct=3,avg=0,alert=19,"
          "shunt_lsb_nv=2500,bus_lsb_uv=1250,coloane=t_us;shunt;bus")


def raw_for_ma(ma):
    # I = shunt × 2,5 µV / 0,1 Ω  →  shunt = I(mA) × 1000 / 25
    return round(ma * 1000 / 25)


def write_capture(path, dur_s, current_at, t0_us=5_000_000, extra=()):
    with open(path, "w") as f:
        f.write("# captura,port=/dev/null,inceput=2026-11-17T12:00:00\n")
        f.write("ets Jun  8 2016 zgomot de pornire\n")
        f.write(HEADER + "\n")
        for line in extra:
            f.write(line + "\n")
        t = t0_us
        while t <= t0_us + dur_s * 1e6:
            f.write(f"{t},{raw_for_ma(current_at(t))},{BUS_RAW}\n")
            t += DT_US


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()


class TestRead(Base):
    def test_conversie(self):
        p = self.dir / "c.csv"
        write_capture(p, 1, lambda t: 10.0, extra=["# pierdute,3", "# erori_i2c,1"])
        cap = ina226.read(p)
        self.assertAlmostEqual(cap.ma[0], 10.0, places=6)
        self.assertAlmostEqual(cap.mv[0], 3750.0)
        self.assertEqual(cap.t[0], 0)
        self.assertEqual((cap.lost, cap.i2c_errors), (3, 1))
        self.assertEqual(cap.meta["shunt_mohm"], "100")

    def test_repornire(self):
        p = self.dir / "r.csv"
        write_capture(p, 1, lambda t: 1.0)
        with open(p, "a") as f:
            f.write(HEADER + "\n10,40,3000\n20,40,3000\n")
        cap = ina226.read(p)
        self.assertEqual(cap.restarts, 1)
        self.assertTrue(all(i == 1.0 for i in cap.ma))

    def test_fara_antet(self):
        p = self.dir / "x.csv"
        p.write_text("1,2,3\n")
        with self.assertRaises(SystemExit):
            ina226.read(p)


class TestSummary(Base):
    def test_constant(self):
        p = self.dir / "c.csv"
        write_capture(p, 10, lambda t: 20.0)
        s = ina226.summary(ina226.read(p))
        self.assertAlmostEqual(s["rata_hz"], 1e6 / DT_US, delta=0.5)
        self.assertEqual(s["goluri"], 0)
        self.assertAlmostEqual(s["ma_medie"], 20.0, places=6)
        self.assertAlmostEqual(s["mw_medie"], 75.0, places=4)
        dur = s["durata_s"]
        self.assertAlmostEqual(s["energie_mj"], 75.0 * dur, places=3)
        self.assertAlmostEqual(s["sarcina_mah"], 20.0 * dur / 3600, places=6)
        self.assertAlmostEqual(s["rezolutie_ua"], 25.0)

    def test_gol(self):
        p = self.dir / "g.csv"
        write_capture(p, 2, lambda t: 1.0)
        lines = p.read_text().splitlines()
        del lines[500:600]
        p.write_text("\n".join(lines) + "\n")
        self.assertEqual(ina226.summary(ina226.read(p))["goluri"], 1)

    def test_integrala_taiata(self):
        t = [0.0, 1.0, 2.0]
        y = [0.0, 2.0, 4.0]                  # y = 2t
        self.assertAlmostEqual(ina226.integrate(t, y), 4.0)
        self.assertAlmostEqual(ina226.integrate(t, y, 0.5, 1.5), 2.0)


class TestPages(Base):
    """Cititorul pornește la t_cititor = 0. Înregistratorul îl vede cu decalajul D:
    t_înregistrator = t_cititor + D. Fiecare întoarcere: 150 mA timp de 400 ms,
    peste un repaus de 5 mA; întoarcerile vin neregulat, ca alinierea să fie unică."""

    D = 7_345_000
    STARTS = [1_000_000, 2_700_000, 3_900_000, 6_100_000, 7_000_000, 9_300_000]
    TURN = 400_000

    def current(self, t):
        tc = t - self.D
        return 150.0 if any(a <= tc < a + self.TURN for a in self.STARTS) else 5.0

    def write_pages(self, path, files=("perf-0001.csv",)):
        with open(path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["fisier", "t_us", "declansare", "completa", "randuri", "intrare", "pregatire",
                        "paginare", "desenare", "spi", "panou", "total"])
            for name in files:
                for a in self.STARTS:
                    # evenimentul vine la 30 ms după front; total se măsoară de la front
                    w.writerow([name, a + 30_000, "buton", 0, 480, 30, 1, 2, 3, 4, 360, self.TURN / 1000])

    def run_cmd(self, *argv):
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = ina226.main([str(a) for a in argv])
        return rc, buf.getvalue()

    def test_aliniere_si_energie(self):
        ina = self.dir / "ina.csv"
        pag = self.dir / "pagini.csv"
        out = self.dir / "energie.csv"
        write_capture(ina, 20, self.current, t0_us=self.D - 2_000_000)
        self.write_pages(pag)
        cap = ina226.read(ina)
        d, q = ina226.align(cap, ina226.read_pages(pag))
        self.assertLessEqual(abs(d - self.D), 2000)
        self.assertGreater(q, 1.5)

        rc, text = self.run_cmd("pagini", ina, "--pagini", pag, "-o", out)
        self.assertEqual(rc, 0)
        self.assertIn("6 întoarceri", text)
        with open(out) as f:
            rows = list(csv.DictReader(f))
        want = 150.0 * 3.75 * 0.4           # mA × V × s = mJ
        for r in rows:
            # ±2 ms de aliniere la marginea dintre 5 și 150 mA
            self.assertAlmostEqual(float(r["energie_mj"]), want, delta=145 * 3.75 * 0.003)

    def test_decalaj_dat(self):
        ina = self.dir / "ina.csv"
        pag = self.dir / "pagini.csv"
        write_capture(ina, 20, self.current, t0_us=self.D - 2_000_000)
        self.write_pages(pag)
        rc, text = self.run_cmd("pagini", ina, "--pagini", pag, "--decalaj-us", self.D)
        self.assertEqual(rc, 0)
        self.assertNotIn("aliniere", text)
        median = float(text.split("median ")[1].split(" mJ")[0].replace(",", "."))
        self.assertAlmostEqual(median, 225.0, delta=0.5)   # marginile cad între eșantioane

    def test_mai_multe_porniri(self):
        pag = self.dir / "pagini.csv"
        self.write_pages(pag, files=("perf-0001.csv", "perf-0002.csv"))
        with self.assertRaises(SystemExit):
            ina226.read_pages(pag)
        self.assertEqual(len(ina226.read_pages(pag, "perf-0002.csv")), len(self.STARTS))


class TestFigure(Base):
    def test_svg(self):
        ina = self.dir / "ina.csv"
        svg = self.dir / "f.svg"
        write_capture(ina, 3, lambda t: 50.0 if (t // 500_000) % 2 else 2.0)
        with redirect_stdout(io.StringIO()):
            ina226.main(["figura", str(ina), "-o", str(svg), "--coloane", "100"])
        text = svg.read_text()
        self.assertTrue(text.startswith("<svg") or "<svg" in text[:200])
        self.assertIn("maximul", text)


if __name__ == "__main__":
    unittest.main()
