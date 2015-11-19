#!/usr/bin/python3

import abrt_p2_testing
from abrt_p2_testing import (wait_for_hooks,
                             DBUS_ERROR_ACCESS_DENIED_DELETE,
                             DBUS_ERROR_BAD_ADDRESS,)


class TestDeleteProblemsSanity(abrt_p2_testing.TestCase):

    def test_delete_problems(self):
        description = {"analyzer"    : "problems2testsuite_analyzer",
                       "type"        : "problems2testsuite_type",
                       "reason"      : "Application has been killed",
                       "backtrace"   : "die()",
                       "executable"  : "/usr/bin/sh",
                       "duphash"     : None,
                       "uuid"        : None}

        description["duphash"] = description["uuid"] = "DEADBEEF"
        one = self.p2.NewProblem(description, 0)
        wait_for_hooks(self)

        description["duphash"] = description["uuid"] = "81680083"
        two = self.p2.NewProblem(description, 0)
        wait_for_hooks(self)

        description["duphash"] = description["uuid"] = "FFFFFFFF"
        three = self.p2.NewProblem(description, 0)
        wait_for_hooks(self)

        p = self.p2.GetProblems()

        self.assertIn(one, p)
        self.assertIn(two, p)
        self.assertIn(three, p)

        self.p2.DeleteProblems([one])

        p = self.p2.GetProblems()

        self.assertNotIn(one, p)
        self.assertIn(two, p)
        self.assertIn(three, p)

        self.assertRaisesDBusError(DBUS_ERROR_BAD_ADDRESS,
                    self.p2.DeleteProblems, [two, three, one])

        p = self.p2.GetProblems()

        self.assertNotIn(one, p)
        self.assertNotIn(two, p)
        self.assertNotIn(three, p)

        self.p2.DeleteProblems([])

        self.assertRaisesDBusError(DBUS_ERROR_BAD_ADDRESS,
                self.p2.DeleteProblems, ["/invalid/path"])

        self.assertRaisesDBusError(DBUS_ERROR_BAD_ADDRESS,
                self.p2.DeleteProblems, ["/org/freedesktop/Problems2/Entry/FAKE"])


if __name__ == "__main__":
    abrt_p2_testing.main(TestDeleteProblemsSanity)
