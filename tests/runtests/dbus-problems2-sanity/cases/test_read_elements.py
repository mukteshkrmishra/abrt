#!/usr/bin/python3

import os
import dbus

import abrt_p2_testing
from abrt_p2_testing import (wait_for_hooks,
                             create_fully_initialized_problem,
                             Problems2Entry)


class TestReadElements(abrt_p2_testing.TestCase):
    def setUp(self):
        self.p2_entry_path = create_fully_initialized_problem(self, self.p2)

    def tearDown(self):
        self.p2.DeleteProblems([self.p2_entry_path])

    def test_read_elements(self):
        requested = { "reason" : dbus.types.String,
                      "hugetext" : dbus.types.UnixFd,
                      "binary" : dbus.types.UnixFd }

        p2e = Problems2Entry(self.bus, self.p2_entry_path)
        elements = p2e.ReadElements(requested.keys(), 0)

        for r, t in requested.items():
            self.assertIn(r, elements)
            self.assertEqual(t, type(elements[r]))

        resp = p2e.ReadElements([], 0x0)
        self.assertTrue(len(resp) == 0, "the response for an empty request is not empty")

        resp = p2e.ReadElements(["foo"], 0x0)
        self.assertTrue(len(resp) == 0, "the response for an request with non-existing element is not empty")

        resp = p2e.ReadElements(["/etc/shadow", "../../../../etc/shadow"], 0x0)
        self.assertTrue(len(resp) == 0, "the response for an request with prohibited elements is not empty")

        reasonlist = p2e.ReadElements(["reason"], 0x08)
        self.assertTrue(len(reasonlist) == 0, "returned text when ONLY_BIG_TEXT requested")

        reasonlist = p2e.ReadElements(["reason"], 0x10)
        self.assertTrue(len(reasonlist) == 0, "returned text when ONLY_BIN requested")

        reasonlist = p2e.ReadElements(["reason"], 0x04)
        self.assertTrue(len(reasonlist) == 1, "not returned text when ONLY_TEXT requested")

        self.assertEqual(reasonlist["reason"], "Application has been killed", "invalid data returned")

        reasonlist = p2e.ReadElements(["reason"], 0x01 | 0x04)
        self.assertTrue(len(reasonlist) == 1, "not returned fd when ALL_FD | ONLY_TEXT requested")

        fd = reasonlist["reason"].take()
        try:
            # try read few more bytes to verify that the file is not broken
            data = os.read(fd, len("Application has been killed") + 10)
            self.assertEqual("Application has been killed", data.decode(),
                    "invalid data read from file descriptor : '%s'" % (data))
        finally:
            os.close(fd)


if __name__ == "__main__":
    abrt_p2_testing.main(TestReadElements)
