#!/usr/bin/python3

import os
import pwd
import dbus
import time
import socket

import abrt_p2_testing
from abrt_p2_testing import (create_fully_initialized_problem, Problems2Entry)


class TestProblemEntryProperties(abrt_p2_testing.TestCase):

    def setUp(self):
        self.p2_entry_path = create_fully_initialized_problem(self, self.p2)

    def tearDown(self):
        self.p2.DeleteProblems([self.p2_entry_path])

    def test_problem_entry_properties(self):
        p2e = Problems2Entry(self.bus, self.p2_entry_path)

        self.assertRegexpMatches(p2e.getproperty("id"),
                "/var/spool/abrt/problems2testsuite_type[^/]*", "strange problem ID")

        self.assertEqual(pwd.getpwuid(os.geteuid()).pw_name, p2e.getproperty("user"), "User name")
        self.assertEqual(socket.gethostname(), p2e.getproperty("hostname"), "hostname")

        self.assertEqual("problems2testsuite_type", p2e.getproperty("type"), "type")
        self.assertEqual("/usr/bin/foo", p2e.getproperty("executable"), "executable")
        self.assertEqual("/usr/bin/foo --blah", p2e.getproperty("command_line_arguments"), "command_line_arguments")
        self.assertEqual("abrt", p2e.getproperty("component"), "component")
        self.assertEqual("FEDCBA9876543210", p2e.getproperty("duphash"), "duphash")
        self.assertEqual("0123456789ABCDEF", p2e.getproperty("uuid"), "uuid")
        self.assertEqual("Application has been killed", p2e.getproperty("reason"), "reason")
        self.assertEqual(os.geteuid(), p2e.getproperty("uid"), "uid")
        self.assertEqual(1, p2e.getproperty("count"), "count")
        self.assertEqual(p2e.getproperty("last_occurrence"), p2e.getproperty("first_occurrence"), "first_occurrence == last_occurrence")

        self.assertLessEqual(abs(p2e.getproperty("first_occurrence") - time.time()), 5,
                "too old first occurrence")

        self.assertLessEqual(abs(p2e.getproperty("last_occurrence") - time.time()), 5,
                "too old last occurrence")

        self.assertEqual(True, p2e.getproperty("is_reported"), "is_reported")
        self.assertEqual(True, p2e.getproperty("can_be_reported"), "can_be_reported")
        self.assertEqual(False, p2e.getproperty("is_remote"), "is_reported")

        package = p2e.getproperty("package")
        self.assertEqual(5, len(package), "insufficient number of package members")

        exp_package = ("problems2-1.2-3", "", "problems2", "1.2", "3")
        self.assertEqual(exp_package, package, "invalid package struct")

        elements = p2e.getproperty("elements")
        self.assertNotEqual(0, len(elements), "Number of elements")

        for e in ["analyzer", "type", "reason", "backtrace", "executable", "uuid",
                  "duphash", "package", "pkg_name", "pkg_version", "pkg_release",
                  "cmdline", "component", "hugetext", "binary", "count", "time"]:
            self.assertIn(e, elements, "Property elements")

        reports = p2e.getproperty("reports")
        self.assertEqual(3, len(reports), "missing reports")

        exp = [
            ("ABRT Server", { "BTHASH" : "0123456789ABCDEF", "MSG" : "test"}),
            ("Server", { "URL" : "http://example.org"}),
            ("Server", { "URL" : "http://case.org"}),
            ]

        for i in range(0, len(exp) - 1):
            self.assertEqual(exp[i][0], reports[i][0], "invalid reported_to label")
            self.assertEqual(exp[i][1], reports[i][1], "invalid reported_to value")


if __name__ == "__main__":
    abrt_p2_testing.main(TestProblemEntryProperties)
