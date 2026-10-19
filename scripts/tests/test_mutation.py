"""Tests for scripts/mutation.py (the mutants it makes from a source text)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import mutation  # noqa: E402


class Mutants(unittest.TestCase):
    def ops(self, text):
        return [(m["line"], m["old"], m["new"]) for m in mutation.mutants(text)]

    def test_relational_and_logical_operators(self):
        self.assertEqual(self.ops("if (a < b && c == d)\n"),
                         [(1, "<", "<="), (1, "&&", "||"), (1, "==", "!=")])

    def test_comments_and_preprocessor_lines_are_left_alone(self):
        self.assertEqual(self.ops("// a < b\n#include <x>\nint x; // y == z\n"), [])

    def test_templates_without_spaces_are_not_operators(self):
        self.assertEqual(self.ops("std::vector<int> v;\nauto p = q->r;\n"), [])

    def test_constants_and_off_by_one(self):
        self.assertEqual(self.ops("return true;\nn = n + 1;\n"), [(1, "true", "false"), (2, "+ 1", "- 1")])

    def test_strings_are_left_alone(self):
        self.assertEqual(self.ops('log("a < b && true");\n'), [])

    def test_apply_changes_only_that_place(self):
        text = "x = a < b;\ny = a < b;\n"
        m = mutation.mutants(text)[1]
        self.assertEqual(mutation.apply(text, m), "x = a < b;\ny = a <= b;\n")


if __name__ == "__main__":
    unittest.main()
