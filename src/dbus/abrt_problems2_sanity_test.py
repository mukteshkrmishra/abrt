#!/usr/bin/python3
import os
import sys
import time
import re
import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

BUS_NAME="org.freedesktop.problems"

DBUS_ERROR_BAD_ADDRESS="org.freedesktop.DBus.Error.BadAddress: Requested Entry does not exist"
DBUS_ERROR_ACCESS_DENIED_READ="org.freedesktop.DBus.Error.AccessDenied: You are not authorized to access the problem"
DBUS_ERROR_ACCESS_DENIED_DELETE="org.freedesktop.DBus.Error.AccessDenied: You are not authorized to delete the problem"

class TestFrame(object):

    def __init__(self, non_root_uid):
        DBusGMainLoop(set_as_default=True)

        self.loop = GLib.MainLoop()

        self.root_bus = dbus.SystemBus(private=True)
        self.root_p2_proxy = self.root_bus.get_object(BUS_NAME, '/org/freedesktop/Problems2')
        self.root_p2 = dbus.Interface(self.root_p2_proxy, dbus_interface='org.freedesktop.Problems2')

        os.seteuid(non_root_uid)

        self.bus = dbus.SystemBus(private=True)
        self.p2_proxy = self.bus.get_object(BUS_NAME, '/org/freedesktop/Problems2')
        self.p2 = dbus.Interface(self.p2_proxy, dbus_interface='org.freedesktop.Problems2')

        self.ac_signal_occurrences = []

    def interrupt_waiting(self, emergency=True):
        self.loop.quit()
        if not emergency:
            GLib.Source.remove(self.tm)

    def handle_authorization_changed(self, status):
        if not "AuthorizationChanged" in self.signals:
            return

        self.interrupt_waiting(False)
        self.ac_signal_occurrences.append(status)

    def handle_crash(self, entry_path, uid):
        if not "Crash" in self.signals:
            return

        self.interrupt_waiting(False)
        self.crash_signal_occurrences.append((entry_path, uid))

    def wait_for_signals(self, signals):
        self.signals = signals
        self.tm = GLib.timeout_add(1000, self.interrupt_waiting)
        self.loop.run()

def expect_dbus_error(error, method, *args):
    try:
        method(*args)
        print("FAILURE: Expected D-Bus error: %s" % (error))
    except dbus.exceptions.DBusException as ex:
        if str(ex) != error:
            print("FAILURE: caught invalid text:\n\tExpected: %s\n\tGot     :%s\n" % (error, str(ex)))

def test_fake_binary_type(tf):
    print("TEST FAKE BINARY TYPE")

    with open("/tmp/fake_type", "w") as type_file:
        type_file.write("CCpp")

    with open("/tmp/fake_type", "r") as type_file:
        description = {"analyzer"    : "libreport",
                       "reason"      : "Application has been killed",
                       "backtrace"   : "die()",
                       "executable"  : "/usr/bin/foo",
                       "type"        : dbus.types.UnixFd(type_file)}

        expect_dbus_error("org.freedesktop.problems.Failure: You are not allowed to create element 'type' containing 'CCpp'",
                              tf.p2.NewProblem, description)


def test_real_problem(tf):
    print("TEST REAL PROBLEM")

    # 25 * 41 = 1025
    data = "ABRT test case huge file " * 41
    with open("/tmp/hugetext", "w") as hugetext_file:
        # 9000KiB > 8MiB
        for i in range(0, 9000):
            hugetext_file.write(data)

    with open("/tmp/hugetext", "r") as hugetext_file:
        with open("/usr/bin/true", "r") as bintrue_file:
            description = {"analyzer"    : "libreport",
                           "type"        : "libreport",
                           "reason"      : "Application has been killed",
                           "backtrace"   : "die()",
                           "executable"  : "/usr/bin/foo",
                           "hugetext"    : dbus.types.UnixFd(hugetext_file),
                           "binary"      : dbus.types.UnixFd(bintrue_file)}

            tf.problem_id = tf.p2.NewProblem(description)
            if not tf.problem_id:
                print("FAILURE : empty return value")


def test_crash_signal(tf):
    print("TEST CRASH SIGNAL")

    if len(tf.crash_signal_occurrences) != 1:
        print("FAILURE : Crash signal wasn't emitted")
    else:
        if tf.crash_signal_occurrences[0][0] != tf.problem_id:
            print("FAILURE : Crash signal was emitted with wrong PATH")
        if tf.crash_signal_occurrences[0][1] != os.geteuid():
            print("FAILURE : Crash signal was emitted with wrong UID")


def test_get_problems(tf):
    print("TEST GET PROBLEMS")

    p = tf.p2.GetProblems()
    if not p:
        print("FAILURE: no problems")

    if not tf.problem_id in p:
        print("FAILURE: missing our problem")

    if tf.private_problem_id in p:
        print("FAILURE: contains private problem")


def test_get_problem_data(tf):
    print("TEST GET PROBLEM DATA")

    #tf.p2.GetProblemData(dbus.types.String())

    expect_dbus_error(DBUS_ERROR_BAD_ADDRESS,
        tf.p2.GetProblemData, "/invalid/path")

    expect_dbus_error(DBUS_ERROR_BAD_ADDRESS,
        tf.p2.GetProblemData, "/org/freedesktop/Problems2/Entry/FAKE")

    expect_dbus_error(DBUS_ERROR_ACCESS_DENIED_READ,
            tf.p2.GetProblemData, tf.private_problem_id)

    p = tf.p2.GetProblemData(tf.problem_id)
    expected = {
        "analyzer"    : (2, len("libreport"), "libreport"),
        "type"        : (2, len("libreport"), "libreport"),
        "reason"      : (2, len("Application has been killed"), "Application has been killed"),
        "backtrace"   : (2, len("die()"), "die()"),
        "executable"  : (2, len("/usr/bin/foo"), "/usr/bin/foo"),
        "hugetext"    : (64, os.path.getsize("/tmp/hugetext"), "/var/spool/abrt/[^/]+/hugetext"),
        "binary"      : (1, os.path.getsize("/usr/bin/true"), "/var/spool/abrt/[^/]+/binary"),
    }

    for k, v in expected.items():
        if not k in p:
            print("FAILURE: missing " + k)
            continue

        g = p[k]
        if not re.match(v[2], g[2]):
            print("FAILURE: invalid contents of '%s'" % (k))

        if g[1] != v[1]:
            print("FAILURE: invalid length '%s' : %i" % (k, g[1]))

        if (g[0] & v[0]) != v[0]:
            print("FAILURE: invalid flags %s : %i" % (k, g[0]))


def test_get_private_problem(tf):
    print("TEST GET PRIVATE PROBLEM")

    p = tf.p2.GetProblems()
    if not p:
        print("FAILURE: no problems")

    if not tf.problem_id in p:
        print("FAILURE: missing our problem")

    if not tf.private_problem_id in p:
        print("FAILURE: missing private problem")

    p = tf.p2.GetProblemData(tf.private_problem_id)

    if p["uid"][2] != "0":
        print("FAILURE: invalid UID")


def test_private_problem_not_accessible(tf):
    print("TEST PRIVATE PROBLEM NOT ACCESSIBLE WHEN SESSION IS CLOSED")

    p = tf.p2.GetProblems()
    if not p:
        print("FAILURE: no problems")

    if not tf.problem_id in p:
        print("FAILURE: missing our problem")

    if tf.private_problem_id in p:
        print("FAILURE: accessible private problem")

    expect_dbus_error(DBUS_ERROR_ACCESS_DENIED_READ,
            tf.p2.GetProblemData, tf.private_problem_id)

    expect_dbus_error(DBUS_ERROR_ACCESS_DENIED_DELETE,
            tf.p2.DeleteProblems, [tf.private_problem_id])


def test_delete_problems(tf):
    print("TEST DELETE PROBLEMS")

    description = {"analyzer"    : "libreport",
                   "type"        : "libreport",
                   "reason"      : "Application has been killed",
                   "backtrace"   : "die()",
                   "executable"  : "/usr/bin/sh",
                   "duphash"     : None,
                   "uuid"        : None}

    description["duphash"] = description["uuid"] = "DEADBEEF"
    one = tf.p2.NewProblem(description)

    description["duphash"] = description["uuid"] = "81680083"
    two = tf.p2.NewProblem(description)

    description["duphash"] = description["uuid"] = "FFFFFFFF"
    three = tf.p2.NewProblem(description)

    p = tf.p2.GetProblems()
    if not(one in p and two in p and three in p):
        print("FAILURE: problems not detected")

    tf.p2.DeleteProblems([one])

    p = tf.p2.GetProblems()
    if one in p:
        print("FAILURE: 'one' not removed")

    if not(two in p and three in p):
        print("FAILURE: 'two' and 'three' disappeared")

    expect_dbus_error(DBUS_ERROR_BAD_ADDRESS,
            tf.p2.DeleteProblems, [two, three, one])

    p = tf.p2.GetProblems()
    if two in p and three in p:
        print("FAILURE: 'two' and 'three' not removed")

    tf.p2.DeleteProblems([])

    expect_dbus_error(DBUS_ERROR_BAD_ADDRESS,
            tf.p2.DeleteProblems, ["/invalid/path"])

    expect_dbus_error(DBUS_ERROR_BAD_ADDRESS,
            tf.p2.DeleteProblems, ["/org/freedesktop/Problems2/Entry/FAKE"])

    expect_dbus_error(DBUS_ERROR_ACCESS_DENIED_DELETE,
            tf.p2.DeleteProblems, [tf.private_problem_id])


def test_get_session(tf):
    print("TEST GET SESSION")

    p2_session_obj = tf.p2.GetSession()
    if p2_session_obj != "/org/freedesktop/Problems2/Session/1":
        print("FAILURE : wrong session path : %s" % (str(p2_session_obj)))

    tf.p2_session_proxy = tf.bus.get_object(BUS_NAME, p2_session_obj)
    tf.p2_session_props = dbus.Interface(tf.p2_session_proxy, dbus_interface=dbus.PROPERTIES_IFACE)
    tf.p2_session = dbus.Interface(tf.p2_session_proxy, dbus_interface='org.freedesktop.Problems2.Session')

    if tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"):
        print("FAILURE : is authorized by default")
    return False


def test_authrorize(tf):
    print("TEST AUTHORIZE")

    if tf.p2_session.Authorize(dbus.types.String(), 1) != 0:
        print("FAILURE : cannot authorize")

    if not tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"):
        print("FAILURE : not authorized")


def test_authrorize_signal(tf):
    print("TEST AUTHORIZE SIGNAL")

    if len(tf.ac_signal_occurrences) != 1:
        print("FAILURE : signal wasn't emitted")

    if len(tf.ac_signal_occurrences) == 1 and tf.ac_signal_occurrences[0] != 0:
        print("FAILURE : signal was emitted with wrong number")


def test_close(tf):
    print("TEST CLOSE")

    tf.p2_session.Close()

    p2_session_obj = tf.p2.GetSession()
    tf.p2_session_proxy = tf.bus.get_object(BUS_NAME, p2_session_obj)
    tf.p2_session_props = dbus.Interface(tf.p2_session_proxy, dbus_interface=dbus.PROPERTIES_IFACE)
    tf.p2_session = dbus.Interface(tf.p2_session_proxy, dbus_interface='org.freedesktop.Problems2.Session')

    if tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"):
        print("FAILURE : still authorized")


def test_close_signal(tf):
    print("TEST CLOSE SIGNAL")

    if len(tf.ac_signal_occurrences) != 2:
        print("FAILURE : signal wasn't emitted")

    if len(tf.ac_signal_occurrences) == 2 and tf.ac_signal_occurrences[1] != 1:
        print("FAILURE : signal was emitted with wrong number")


def create_problem(p2):
    with open("/usr/bin/true", "r") as bintrue_file:
        description = {"analyzer"    : "libreport",
                       "type"        : "libreport",
                       "reason"      : "Application has been killed",
                       "backtrace"   : "die()",
                       "executable"  : "/usr/bin/foo",}

        return p2.NewProblem(description)


if __name__ == "__main__":
    if os.getuid() != 0:
        print("Run this test under root!")
        sys.exit(1)

    if len(sys.argv) != 2:
        print("Pass an uid of non-root user as the first argument!")
        sys.exit(1)

    non_root_uid = int(sys.argv[1])
    tf = TestFrame(non_root_uid)

    test_fake_binary_type(tf)

    tf.crash_signal_occurrences = []
    tf.p2.connect_to_signal("Crash", tf.handle_crash)

    test_real_problem(tf)
    tf.wait_for_signals(["Crash"])

    tf.private_problem_id = create_problem(tf.root_p2)

    test_get_problems(tf)
    test_get_problem_data(tf)
    test_delete_problems(tf)
    test_get_session(tf)

    tf.ac_signal_occurrences = []
    tf.p2_session.connect_to_signal("AuthorizationChanged", tf.handle_authorization_changed)

    test_authrorize(tf)

    tf.wait_for_signals(["AuthorizationChanged"])

    test_authrorize_signal(tf)
    test_get_private_problem(tf)
    test_close(tf)

    tf.wait_for_signals(["AuthorizationChanged"])

    test_close_signal(tf)
    test_private_problem_not_accessible(tf)
