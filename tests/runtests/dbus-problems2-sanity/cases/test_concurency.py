#!/usr/bin/python3

import dbus

import abrt_p2_testing
from abrt_p2_testing import (BUS_NAME, get_huge_file_path,)


class TestConcurency(abrt_p2_testing.TestCase):

    def setUp(self):
        self.p2_entry_path = None
        self.p2_entry_root_path = None

    def tearDown(self):
        if self.p2_entry_path:
            self.p2.DeleteProblems([self.p2_entry_path])

        if self.p2_entry_root_path:
            self.root_p2.DeleteProblems([self.p2_entry_root_path])

    def test_new_problem(self):
        self.replies = 4

        def reply_handle_cb(path):
            print("Reply handler")
            print(path);
            self.replies -= 1
            if self.replies <= 0:
                self.interrupt_waiting()

        def error_handle_cb(ex):
            print("Error handler")
            print(str(ex))
            self.replies -= 1
            if self.replies <= 0:
                self.interrupt_waiting()

        huge_file_path = get_huge_file_path()

        buses = list()
        while len(buses) < self.replies :
            huge_file = open(huge_file_path, "r")
            description = {"analyzer"    : "problems2testsuite_analyzer",
                           "reason"      : "Application has been killed",
                           "backtrace"   : "die()",
                           "duphash"     : "NEW_PROBLEM_DATA_SIZE",
                           "uuid"        : "NEW_PROBLEM_DATA_SIZE",
                           "huge_file"   : dbus.types.UnixFd(huge_file),
                           "executable"  : "/usr/bin/foo",
                           "type"        : "abrt-problems2-sanity"}

            bus = dbus.SystemBus(private=True)
            p2_proxy = bus.get_object(BUS_NAME, '/org/freedesktop/Problems2')
            p2 = dbus.Interface(p2_proxy, dbus_interface='org.freedesktop.Problems2')

            buses.append((bus, p2, description, huge_file))

        for _, p2, description, _ in buses:
            p2.NewProblem(description, 0, reply_handler=reply_handle_cb, error_handler=error_handle_cb)

        self.main_loop_start(timeout=200000)

        for _, _, _, huge_file in buses:
            huge_file.close()


if __name__ == "__main__":
    abrt_p2_testing.main(TestConcurency)
