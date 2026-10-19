"""Tests for scripts/hil_check.py (pass/fail rules of the automated hardware run scripts/hil.sh)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import hil_check  # noqa: E402

GRID = [8.0493 + k for k in range(12)]  # a clean real-time run on the TI Desktop (2026-10-19)


class Grid(unittest.TestCase):
    def test_clean_run_passes(self):
        r = hil_check.grid(GRID, 1.0, 20.0)
        self.assertEqual(r["missing"], 0)
        self.assertEqual(r["offgrid"], 0)
        self.assertLess(r["maxdev_ms"], 0.1)
        self.assertTrue(all(ok for _, ok, _ in hil_check.grid_verdicts(r)))

    def test_leftover_sound_before_the_first_note_is_ignored(self):
        # cal2: a release tail of the run before at 0.06 s, then the notes from 5 s on
        r = hil_check.grid([0.0555] + [5.0493 + k for k in range(35)], 1.0, 40.0)
        self.assertEqual(r["missing"], 0)
        self.assertEqual(r["first_s"], 5.0493)

    def test_a_lost_note_fails(self):
        on = GRID[:5] + GRID[6:]
        r = hil_check.grid(on, 1.0, 20.0)
        self.assertEqual(r["missing"], 1)
        self.assertIn(("notes: none lost", False), [(n, ok) for n, ok, _ in hil_check.grid_verdicts(r)])

    def test_a_late_note_fails(self):
        on = list(GRID)
        on[6] += 0.012  # 12 ms late
        r = hil_check.grid(on, 1.0, 20.0)
        self.assertGreater(r["maxdev_ms"], 10)
        self.assertIn(("timing: every note within 5 ms of the grid", False),
                      [(n, ok) for n, ok, _ in hil_check.grid_verdicts(r)])

    def test_an_extra_note_after_the_start_fails(self):
        on = sorted(GRID + [12.5])
        r = hil_check.grid(on, 1.0, 20.0)
        self.assertEqual(r["offgrid"], 1)

    def test_first_note_too_late_fails(self):
        r = hil_check.grid([15.0493 + k for k in range(5)], 1.0, 20.0)
        self.assertIn(("start: first note within 12 s", False),
                      [(n, ok) for n, ok, _ in hil_check.grid_verdicts(r)])

    def test_from_judges_only_the_notes_after_a_wake(self):
        # a suspend at 25 s: a burst of catch-up sound, then the grid again
        on = [5.05 + k for k in range(20)] + [25.3, 25.31, 25.9] + [70.05 + k for k in range(50)]
        r = hil_check.grid(on, 1.0, 120.0, since=70.0)
        self.assertEqual((r["missing"], r["offgrid"]), (0, 0))
        self.assertTrue(all(ok for n, ok, _ in hil_check.grid_verdicts(r) if not n.startswith("start")))

    def test_silence_fails(self):
        r = hil_check.grid([], 1.0, 20.0)
        self.assertFalse(all(ok for _, ok, _ in hil_check.grid_verdicts(r)))


LOG_OK = """Virus TI native: Virus ready after 1.1 s
Virus TI native: sync marker at frame 316404 (ping at 315577, 827 frames later)
"""


class Log(unittest.TestCase):
    def verdicts(self, text):
        return {n: ok for n, ok, _ in hil_check.log_verdicts(text)}

    def test_clean_start(self):
        self.assertTrue(all(self.verdicts(LOG_OK).values()))

    def test_no_ready_line_fails(self):
        self.assertFalse(self.verdicts(LOG_OK.splitlines()[1])["log: Virus ready"])

    def test_audio_stood_still_fails(self):
        v = self.verdicts(LOG_OK + "Virus TI native: the Virus' audio stood still (sleep?) - set-up\n")
        self.assertFalse(v["log: audio never stood still"])

    def test_stood_still_allowed_after_a_sleep(self):
        v = {n: ok for n, ok, _ in hil_check.log_verdicts(
            LOG_OK + "Virus TI native: the Virus' audio stood still (sleep?)\n", sleep=True)}
        self.assertTrue(all(v.values()))

    def test_missing_marker_fails(self):
        self.assertFalse(self.verdicts(LOG_OK.splitlines()[0] + "\n")["log: sync marker"])


class Offline(unittest.TestCase):
    def test_paced_render_passes(self):
        out = "offline part: 28.00 s of audio in 27.99 s wall time (1.00x)\n"
        self.assertTrue(all(ok for _, ok, _ in hil_check.offline_verdicts(out)))

    def test_render_running_free_fails(self):
        out = "offline part: 28.00 s of audio in 2.10 s wall time (13.33x)\n"
        self.assertFalse(all(ok for _, ok, _ in hil_check.offline_verdicts(out)))

    def test_no_offline_line_fails(self):
        self.assertFalse(all(ok for _, ok, _ in hil_check.offline_verdicts("wrote x.wav\n")))


if __name__ == "__main__":
    unittest.main()
