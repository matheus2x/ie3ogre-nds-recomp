"""Tests for tools/compare_dumps.py helpers."""

import unittest

from compare_dumps import diff_runs


class DiffRunsTest(unittest.TestCase):
    def test_equal(self):
        self.assertEqual(diff_runs(b"abcd", b"abcd"), [])

    def test_merges_close_runs(self):
        self.assertEqual(diff_runs(b"aaaaaaaa", b"abaabaaa", gap=4), [(1, 4)])

    def test_splits_far_runs(self):
        self.assertEqual(diff_runs(b"a" * 12, b"b" + b"a" * 10 + b"b", gap=4), [(0, 1), (11, 1)])

    def test_length_mismatch(self):
        self.assertEqual(diff_runs(b"abc", b"abcde"), [(3, 2)])


if __name__ == "__main__":
    unittest.main()
