#!/usr/bin/python3
import os
import sys
import time
import re
import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

BUS_NAME="org.freedesktop.problems"


class TestFrame(object):

    def __init__(self):
        DBusGMainLoop(set_as_default=True)

        self.loop = GLib.MainLoop()
        self.bus = dbus.SystemBus()
        self.p2_proxy = self.bus.get_object(BUS_NAME,
                       '/org/freedesktop/Problems2')

        self.p2 = dbus.Interface(self.p2_proxy, dbus_interface='org.freedesktop.Problems2')

        self.ac_signal_occurrences = []

    def interrupt_waiting(self, emergency=True):
        self.loop.quit()
        if not emergency:
            GLib.Source.remove(self.tm)

    def handle_authorization_changed(self, status):
        self.interrupt_waiting(False)
        self.ac_signal_occurrences.append(status)

    def wait_for_signals(self):
        self.tm = GLib.timeout_add(1000, self.interrupt_waiting)
        self.loop.run()


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

        try:
            tf.p2.NewProblem(description)
            print("FAILURE : an exception expected")
        except dbus.exceptions.DBusException as ex:
            if str(ex) != "org.freedesktop.problems.Failure: You are not allowed to create element 'type' containing 'CCpp'":
                print("FAILURE : wrong message : %s" % (str(ex)))
    return False


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
    return False

def test_get_problems(tf):
    print("TEST GET PROBLEMS")

    p = tf.p2.GetProblems()
    if not p:
        print("FAILURE: no problems")
    if not tf.problem_id in p:
        print("FAILURE: missing our problem")
    return False

def test_get_problem_data(tf):
    print("TEST GET PROBLEM DATA")

    #tf.p2.GetProblemData(dbus.types.String())

    try:
        tf.p2.GetProblemData("/invalid/path")
        print("FAILURE: did not detected invalid entry address")
    except dbus.exceptions.DBusException as ex:
        if str(ex) != "org.freedesktop.DBus.Error.BadAddress: Requested Entry does not exist":
            print("FAILURE: invalid exception error")

    try:
        tf.p2.GetProblemData("/org/freedesktop/Problems2/Entry/FAKE")
        print("FAILURE: did not detected invalid entry address")
    except dbus.exceptions.DBusException as ex:
        if str(ex) != "org.freedesktop.DBus.Error.BadAddress: Requested Entry does not exist":
            print("FAILURE: invalid exception error")

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

    try:
        tf.p2.DeleteProblems([two, three, one])
        print("FAILURE: did not detected invalid entry address")
    except dbus.exceptions.DBusException as ex:
        if str(ex) != "org.freedesktop.DBus.Error.BadAddress: Requested Entry does not exist":
            print("FAILURE: invalid exception error")

    p = tf.p2.GetProblems()
    if two in p and three in p:
        print("FAILURE: 'two' and 'three' not removed")

    tf.p2.DeleteProblems([])

    try:
        tf.p2.DeleteProblems(["/invalid/path"])
        print("FAILURE: did not detected invalid entry address")
    except dbus.exceptions.DBusException as ex:
        if str(ex) != "org.freedesktop.DBus.Error.BadAddress: Requested Entry does not exist":
            print("FAILURE: invalid exception error")

    try:
        tf.p2.DeleteProblems(["/org/freedesktop/Problems2/Entry/FAKE"])
        print("FAILURE: did not detected invalid entry address")
    except dbus.exceptions.DBusException as ex:
        if str(ex) != "org.freedesktop.DBus.Error.BadAddress: Requested Entry does not exist":
            print("FAILURE: invalid exception error")


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

    time.sleep(1)

    return False


def test_authrorize_signal(tf):
    print("TEST AUTHORIZE SIGNAL")

    if len(tf.ac_signal_occurrences) != 1:
        print("FAILURE : signal wasn't emitted")

    if len(tf.ac_signal_occurrences) == 1 and tf.ac_signal_occurrences[0] != 0:
        print("FAILURE : signal was emitted with wrong number")
    return False


def test_close(tf):
    print("TEST CLOSE")

    tf.p2_session.Close()

    p2_session_obj = tf.p2.GetSession()
    tf.p2_session_proxy = tf.bus.get_object(BUS_NAME, p2_session_obj)
    tf.p2_session_props = dbus.Interface(tf.p2_session_proxy, dbus_interface=dbus.PROPERTIES_IFACE)
    tf.p2_session = dbus.Interface(tf.p2_session_proxy, dbus_interface='org.freedesktop.Problems2.Session')

    if tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"):
        print("FAILURE : still authorized")

    return False


def test_close_signal(tf):
    print("TEST CLOSE SIGNAL")

    if len(tf.ac_signal_occurrences) != 2:
        print("FAILURE : signal wasn't emitted")

    if len(tf.ac_signal_occurrences) == 2 and tf.ac_signal_occurrences[1] != 1:
        print("FAILURE : signal was emitted with wrong number")
    return False


tf = TestFrame()

test_fake_binary_type(tf)
test_real_problem(tf)
test_get_problem_data(tf)
test_get_problems(tf)
test_delete_problems(tf)

test_get_session(tf)

tf.ac_signal_occurrences = []
tf.p2_session.connect_to_signal("AuthorizationChanged", tf.handle_authorization_changed)

test_authrorize(tf)

tf.wait_for_signals()

test_authrorize_signal(tf)

test_close(tf)

tf.wait_for_signals()

test_close_signal(tf)

