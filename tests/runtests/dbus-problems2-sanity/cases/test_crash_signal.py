#!/usr/bin/python3

import os

import abrt_p2_testing
from abrt_p2_testing import (create_problem)

class TestCrashSanity(abrt_p2_testing.TestCase):

    def setUp(self):
        self.p2.connect_to_signal("Crash", self.handle_crash)

        self.crash_signal_occurrences = []
        self.p2_entry_path = None
        self.p2_entry_root_path = None

    def tearDown(self):
        if self.p2_entry_path:
            self.p2.DeleteProblems([self.p2_entry_path])

        if self.p2_entry_root_path:
            self.root_p2.DeleteProblems([self.p2_entry_root_path])

    def test_user_crash_signal(self):
        self.p2_entry_path = create_problem(self, self.p2)

        self.wait_for_signals(["Crash"])

        if self.assertTrue(len(self.crash_signal_occurrences) == 1, "Crash signal wasn't emitted"):
            self.assertEqual(self.p2_entry_path, self.crash_signal_occurrences[0][0], "Crash signal was emitted with wrong PATH")
            self.assertEqual(os.geteuid(),  self.crash_signal_occurrences[0][1], "Crash signal was emitted with wrong UID")

    def test_foreign_crash_signal(self):
        self.p2_entry_root_path = create_problem(self, self.root_p2)

        self.wait_for_signals(["Crash"])

        if self.assertTrue(len(self.crash_signal_occurrences) == 1, "Crash signal for root's problem wasn't emitted"):
            self.assertEqual(self.p2_entry_root_path, self.crash_signal_occurrences[0][0], "Crash signal was emitted with wrong PATH")
            self.assertEqual(0, self.crash_signal_occurrences[0][1], "Crash signal was emitted with wrong UID")


if __name__ == "__main__":
    abrt_p2_testing.main(TestCrashSanity)
