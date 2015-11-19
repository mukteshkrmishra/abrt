#!/usr/bin/python3

import os
import dbus

import abrt_p2_testing
from abrt_p2_testing import (wait_for_hooks,
                             get_huge_file_path,
                             create_fully_initialized_problem,
                             Problems2Entry,
                             DBUS_LIMIT_ELEMENTS_COUNT,)


class TestNewProblemSanity(abrt_p2_testing.TestCase):

    def setUp(self):
        self.p2_entry_path = None
        self.p2_entry_root_path = None

    def tearDown(self):
        if self.p2_entry_path:
            self.p2.DeleteProblems([self.p2_entry_path])

        if self.p2_entry_root_path:
            self.root_p2.DeleteProblems([self.p2_entry_root_path])

    def test_fake_binary_type(self):
        with open("/tmp/fake_type", "w") as type_file:
            type_file.write("CCpp")

        with open("/tmp/fake_type", "r") as type_file:
            description = {"analyzer"    : "problems2testsuite_analyzer",
                           "reason"      : "Application has been killed",
                           "backtrace"   : "die()",
                           "duphash"     : "FAKE_BINARY_TYPE",
                           "uuid"        : "FAKE_BINARY_TYPE",
                           "executable"  : "/usr/bin/foo",
                           "type"        : dbus.types.UnixFd(type_file)}

            self.assertRaisesDBusError("org.freedesktop.DBus.Error.InvalidArgs: Element 'type' must be of 's' D-Bus type",
                                  self.p2.NewProblem, description, 0)


    def test_not_allowed_elements(self):
        description = {"analyzer"    : "problems2testsuite_analyzer",
                       "type"        : "Kerneloops",
                       "reason"      : "Application has been killed",
                       "duphash"     : "NOT_ALLOWED_ELEMENTS",
                       "uuid"        : "NOT_ALLOWED_ELEMENTS",
                       "backtrace"   : "Machine Check Exception: fake" }

        self.assertRaisesDBusError("org.freedesktop.DBus.Error.InvalidArgs: You are not allowed to create element 'type' containing 'Kerneloops'",
                              self.p2.NewProblem, description, 0)

        self.p2_entry_root_path = self.root_p2.NewProblem(description, 0)
        wait_for_hooks(self)
        self.assertTrue(self.p2_entry_root_path, "root is not allowed to create type=CCpp")

    def test_real_problem(self):
        self.p2_entry_path = create_fully_initialized_problem(self, self.p2)
        self.assertTrue(self.p2_entry_path, "Failed to return ID of the new problem")

    def test_new_problem_sanitized_uid(self):
        description = {"analyzer"    : "problems2testsuite_analyzer",
                       "type"        : "sanitized-uid",
                       "uid"         : "0",
                       "reason"      : "Application has been killed",
                       "duphash"     : "SANITIZED_UID",
                       "backtrace"   : "die()",
                       "executable"  : "/usr/bin/foo" }

        self.p2_entry_path = self.p2.NewProblem(description, 0)
        wait_for_hooks(self)
        self.assertTrue(self.p2_entry_path, "Failed to create problem with uid 0")

        p2e = Problems2Entry(self.bus, self.p2_entry_path)
        self.assertEqual(os.geteuid(), p2e.getproperty("uid"), "Sanitized UID")

    def test_new_problem_sane_default_elements(self):
        description = {}

        self.p2_entry_path = self.p2.NewProblem(description, 0)
        wait_for_hooks(self)
        self.assertTrue(self.p2_entry_path, "Failed to create problem without elements")

        p2e = Problems2Entry(self.bus, self.p2_entry_path)
        self.assertEqual("libreport", p2e.getproperty("type"), "Created type")
        self.assertTrue(p2e.getproperty("uuid"), "Created UUID")

        resp = p2e.ReadElements(["analyzer"], 0)
        if self.assertIn("analyzer", resp, "Created analyzer element"):
            self.assertEqual("libreport", resp["analyzer"])

    def test_new_problem_elements_count_limit(self):
        too_many_elements = dict()
        for i in range(DBUS_LIMIT_ELEMENTS_COUNT + 1):
            too_many_elements[str(i)] = str(i)

        self.assertRaisesDBusError("org.freedesktop.DBus.Error.LimitsExceeded: Too many elements",
                        self.p2.NewProblem, too_many_elements, 0)

    def test_new_problem_data_size_limit(self):
        huge_file_path = get_huge_file_path()

        with open(huge_file_path, "r") as huge_file:
            description = {"analyzer"    : "problems2testsuite_analyzer",
                           "reason"      : "Application has been killed",
                           "backtrace"   : "die()",
                           "duphash"     : "FAKE_BINARY_TYPE",
                           "uuid"        : "FAKE_BINARY_TYPE",
                           "huge_file"   : dbus.types.UnixFd(huge_file),
                           "executable"  : "/usr/bin/foo",
                           "type"        : "abrt-problems2-sanity"}

            self.assertRaisesDBusError("org.freedesktop.DBus.Error.LimitsExceeded: Problem data is too big",
                                  self.p2.NewProblem, description, 0)


if __name__ == "__main__":
    abrt_p2_testing.main(TestNewProblemSanity)
