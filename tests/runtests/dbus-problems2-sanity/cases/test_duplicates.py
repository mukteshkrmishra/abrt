#!/usr/bin/python3

import dbus

import abrt_p2_testing
from abrt_p2_testing import (wait_for_hooks,)


class TestDuplicates(abrt_p2_testing.TestCase):

    def setUp(self):
        self.p2_entry_path = None
        self.p2_entry_duplicate_path = None

    def tearDown(self):
        pass
        if self.p2_entry_path:
            self.p2.DeleteProblems([self.p2_entry_path])

        if self.p2_entry_duplicate_path:
            self.p2.DeleteProblems([self.p2_entry_duplicate_path])

    def test_duplicates(self):
        description = {"analyzer"    : "problems2testsuite_analyzer",
                       "reason"      : "Application has been killed",
                       "backtrace"   : "die()",
                       "duphash"     : "NEW_PROBLEM_DUPLICATES",
                       "uuid"        : "NEW_PROBLEM_DUPLICATES",
                       "executable"  : "/usr/bin/true",
                       "type"        : "abrt-problems2-dupes"}

        self.p2_entry_path = self.p2.NewProblem(description, 0)
        wait_for_hooks(self)

        self.p2_entry_duplicate_path = self.p2.NewProblem(description, 0)
        self.assertEqual(self.p2_entry_path, self.p2_entry_duplicate_path)
        self.p2_entry_duplicate_path = None


if __name__ == "__main__":
    abrt_p2_testing.main(TestDuplicates)
