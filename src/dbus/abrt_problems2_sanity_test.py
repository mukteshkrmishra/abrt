#!/usr/bin/python3
import os
import time
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

    with open("/etc/services", "r") as services_file:
        description = {"analyzer"    : "libreport",
                       "type"        : "libreport",
                       "reason"      : "Application has been killed",
                       "backtrace"   : "die()",
                       "executable"  : "/usr/bin/foo",
                       "services"    : dbus.types.UnixFd(services_file)}

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
test_get_problems(tf)
test_get_session(tf)

tf.ac_signal_occurrences = []
tf.p2_session.connect_to_signal("AuthorizationChanged", tf.handle_authorization_changed)

test_authrorize(tf)

tf.wait_for_signals()

test_authrorize_signal(tf)

test_close(tf)

tf.wait_for_signals()

test_close_signal(tf)

