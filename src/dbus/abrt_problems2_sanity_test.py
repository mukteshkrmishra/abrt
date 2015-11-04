#!/usr/bin/python3
import os
import sys
import time
import re
import dbus
import dbus.service
import pwd
import socket
import random
import string
import logging
import subprocess
from functools import partial
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib
from contextlib import contextmanager

BUS_NAME="org.freedesktop.problems"

DBUS_ERROR_BAD_ADDRESS="org.freedesktop.DBus.Error.BadAddress: Requested Entry does not exist"
DBUS_ERROR_ACCESS_DENIED_READ="org.freedesktop.DBus.Error.AccessDenied: You are not authorized to access the problem"
DBUS_ERROR_ACCESS_DENIED_DELETE="org.freedesktop.DBus.Error.AccessDenied: You are not authorized to delete the problem"

DBUS_LIMIT_ELEMENTS_COUNT = 100
DBUS_LIMIT_DATA_SIZE = 2 * 1024 * 1024 * 1024

class PolkitAuthenticationAgent(dbus.service.Object):
    def __init__(self, bus, subject_bus_name):
        self._object_path = '/org/freedesktop/PolicyKit1/AuthenticationAgent'
        self._replies = list()

        start_time = "0"
        with open("/proc/self/stat") as stat:
            tokens = stat.readline().split(" ")
            start_time = tokens[21]

        self._bus = bus
        self._subject = ('unix-process',
                {'pid' : dbus.types.UInt32(os.getpid()),
                 'start-time' : dbus.types.UInt64(int(start_time))})

        self._authority_proxy = None
        self._authority = None

        dbus.service.Object.__init__(self, self._bus, self._object_path)

    def register(self):
        if not self._authority is None:
            logging.error("Polkit AuthenticationAgent : Already registered")
            return

        proxy = self._bus.get_object('org.freedesktop.PolicyKit1', '/org/freedesktop/PolicyKit1/Authority')
        authority = dbus.Interface(proxy, dbus_interface='org.freedesktop.PolicyKit1.Authority')
        authority.RegisterAuthenticationAgent(self._subject, "en_US", self._object_path)

        logging.debug("Polkit AuthenticationAgent registered")

        self._authority_proxy = proxy
        self._authority = authority

    def unregister(self):
        if self._authority is None:
            logging.error("Polkit AuthenticationAgent : Not registered")
            return

        self._authority.UnregisterAuthenticationAgent(self._subject, self._object_path)

        logging.debug("Polkit AuthenticationAgent unregistered")

        self._authority_proxy = None
        self._authority = None

    def set_replies(self, replies):
        self._replies = replies

    def _get_authorization_reply(self):
        if len(self._replies) == 0:
            logging.warning("Polkit AuthenticationAgent: no reply registered")
            return False

        cb = self._replies.pop(0)
        try:
            return cb()
        except dbus.exceptions.DBusException as ex:
            logging.debug("Polkit AuthenticationAgent: callback raised an DBusException: %s" % (str(ex)))
            raise ex
        except Exception as ex:
            logging.exception(str(ex))

        return False

    @dbus.service.method(dbus_interface="org.freedesktop.PolicyKit1.AuthenticationAgent",
                         in_signature='sssa{ss}saa{sa{sv}}', out_signature='')
    def BeginAuthentication(self, action_id, message, icon_name, details, cookie, identities):
        # all Exceptions in this function are silently ignore
        logging.debug("Polkit AuthenticationAgent: BeginAuthentication : %s" % (cookie))

        if not self._get_authorization_reply():
            logging.debug("Dismissed the authorization request")
            raise dbus.exceptions.DBusException("org.freedesktop.PolicyKit1.Error.Cancelled")

        logging.debug("Acknowledged the authorization request")
        self._authority.AuthenticationAgentResponse2(0, cookie, identities[0])

    @dbus.service.method(dbus_interface="org.freedesktop.PolicyKit1.AuthenticationAgent",
                         in_signature='s', out_signature='')
    def CancelAuthentication(self, cookie):
        # all Exceptions in this function are silently ignore
        logging.warning("Cancel %s" % (cookie))


@contextmanager
def start_polkit_agent(bus, subject_bus_name):
    pk_agent = PolkitAuthenticationAgent(bus, subject_bus_name)
    pk_agent.register()
    yield pk_agent
    pk_agent.unregister()


class Problems2Entry(object):

    def __init__(self, bus, entry_path):
        entry_proxy = bus.get_object(BUS_NAME, entry_path)
        self._properties = dbus.Interface(entry_proxy, dbus_interface="org.freedesktop.DBus.Properties")
        self._entry = dbus.Interface(entry_proxy, dbus_interface="org.freedesktop.Problems2.Entry")

    def __getattribute__(self, name):
        try:
            return object.__getattribute__(self, name)
        except AttributeError:
            entry = object.__getattribute__(self, "_entry")
            return entry.get_dbus_method(name)

    def getproperty(self, name):
        properties = object.__getattribute__(self, "_properties")
        return properties.Get("org.freedesktop.Problems2.Entry", name)


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

        self.p2_session = None

        self.ac_signal_occurrences = []
        self.loop_counter = 0
        self.loop_running = False
        self.tm = -1
        self.signals = []

    def main_loop_start(self):
        self.loop_counter += 1
        if self.loop_running:
            return

        self.loop_running = True
        self.tm = GLib.timeout_add(10000, self.interrupt_waiting)
        self.loop.run()

    def interrupt_waiting(self, emergency=True):
        self.loop_counter -= 1
        if not emergency and self.loop_counter != 0:
            return

        self.loop_running = False
        self.loop.quit()
        if not emergency:
            GLib.Source.remove(self.tm)

    def handle_authorization_changed(self, status):
        if not "AuthorizationChanged" in self.signals:
            return

        logging.debug("Received AuthorizationChanged signal : %d" % (status))

        self.interrupt_waiting(False)
        self.ac_signal_occurrences.append(status)

    def handle_crash(self, entry_path, uid):
        if not "Crash" in self.signals:
            return

        logging.debug("Received Crash signal : UID=%s; PATH=%s" % (uid, entry_path))

        self.interrupt_waiting(False)
        self.crash_signal_occurrences.append((entry_path, uid))

    def wait_for_signals(self, signals):
        self.signals = signals
        logging.debug("Waiting for signals %s" % (", ".join(signals)))
        self.main_loop_start()


def error(message):
    sys.stderr.write("ERROR: ")
    sys.stderr.write(message)
    sys.stderr.write("\n")


def assert_equals(expected, real_value, description="Values are not equal"):
    retval = expected == real_value
    if not retval:
        error("%s: \n\tExpected: %s\n\tGot     : %s" % (description, expected, real_value))
    return retval


def assert_not_equals(banned, real_value, description="Value is invalid"):
    retval = banned != real_value
    if not retval:
        error("%s: \n\tMust not be: %s\n" % (description, real_value))
    return retval


def assert_lower_than(expected, real_value, description="Value should be lower"):
    retval = expected > real_value
    if not retval:
        error("%s: \n\tExpected: %s\n\tGot     : %s" % (description, expected, real_value))
    return retval


def assert_in(e, elements, description="Missing element"):
    retval = e in elements
    if not retval:
        error("%s: \n\tList is missing element: %s" % (description, e))
    return retval


def assert_not_in(e, elements, description="Missing element"):
    retval = e in elements
    if retval:
        error("%s: \n\tList is contains element: %s" % (description, e))
    return not retval


def assert_true(cond, message):
    if not cond:
        error(message)
    return cond


def assert_false(cond, message):
    return assert_true(not cond, message)


def expect_dbus_error(error_msg, method, *args):
    retval = None
    try:
        retval = method(*args)
        error("Expected D-Bus error: %s" % (error_msg))
        return (False, None)
    except dbus.exceptions.DBusException as ex:
        return (assert_equals(error_msg, str(ex), "Exception has invalid text"), retval)


def dictionary_key_has_value(dictionary, key, expected):
    if not key in dictionary:
        error("missing '%s'" % (key))
    elif dictionary[key] != expected:
        error("key '%s', expected: '%s', is:'%s'" % (key, expected, dictionary[key]))


def test_fake_binary_type(tf):
    print("TEST FAKE BINARY TYPE")

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

        expect_dbus_error("org.freedesktop.DBus.Error.InvalidArgs: Element 'type' must be of 's' D-Bus type",
                              tf.p2.NewProblem, description)


def test_not_allowed_elements(tf):
    print("TEST NOT ALLOWED ELEMENTS")

    description = {"analyzer"    : "problems2testsuite_analyzer",
                   "type"        : "CCpp",
                   "reason"      : "Application has been killed",
                   "duphash"     : "NOT_ALLOWED_ELEMENTS",
                   "uuid"        : "NOT_ALLOWED_ELEMENTS",
                   "backtrace"   : "die()",
                   "executable"  : "/usr/bin/foo" }

    expect_dbus_error("org.freedesktop.DBus.Error.InvalidArgs: You are not allowed to create element 'type' containing 'CCpp'",
                          tf.p2.NewProblem, description)

    pr_id = tf.root_p2.NewProblem(description)
    time.sleep(1)
    if not pr_id:
        error("root is not allowed to create type=CCpp")


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
            description = {"analyzer"    : "problems2testsuite_analyzer",
                           "type"        : "problems2testsuite_type",
                           "reason"      : "Application has been killed",
                           "backtrace"   : "die()",
                           "executable"  : "/usr/bin/foo",
                           "uuid"        : "0123456789ABCDEF",
                           "duphash"     : "FEDCBA9876543210",
                           "package"     : "problems2-1.2-3",
                           "pkg_name"    : "problems2",
                           "pkg_version" : "1.2",
                           "pkg_release" : "3",
                           "cmdline"     : "/usr/bin/foo --blah",
                           "component"   : "abrt",
                           "reported_to" : "ABRT Server: BTHASH=0123456789ABCDEF MSG=test\nServer: URL=http://example.org\nServer: URL=http://case.org\n",
                           "hugetext"    : dbus.types.UnixFd(hugetext_file),
                           "binary"      : dbus.types.UnixFd(bintrue_file)}

            tf.problem_first_occurrence = time.time()
            tf.problem_id = tf.p2.NewProblem(description)
            time.sleep(1)
            if not tf.problem_id:
                error("empty return value")


def test_new_problem_sanitized_uid(tf):
    print("TEST SANITIZED UUID")

    description = {"analyzer"    : "problems2testsuite_analyzer",
                   "type"        : "sanitized-uid",
                   "uid"         : "0",
                   "reason"      : "Application has been killed",
                   "duphash"     : "SANITIZED_UID",
                   "backtrace"   : "die()",
                   "executable"  : "/usr/bin/foo" }

    pr_id = tf.p2.NewProblem(description)
    time.sleep(1)
    assert_true(pr_id, "Failed to create problem with uid 0")

    p2e = Problems2Entry(tf.bus, pr_id)
    assert_equals(os.geteuid(), p2e.getproperty("uid"), "Sanitized UID")

    tf.p2.DeleteProblems([pr_id])


def test_new_problem_sanitized_elements(tf):
    print("TEST SANITIZED ELEMENTS")

    description = {}

    pr_id = tf.p2.NewProblem(description)
    time.sleep(1)
    assert_true(pr_id, "Failed to create problem without elements")

    p2e = Problems2Entry(tf.bus, pr_id)
    assert_equals("libreport", p2e.getproperty("type"), "Created type")
    assert_true(p2e.getproperty("uuid"), "Created UUID")

    resp = p2e.ReadElements(["analyzer"], 0)
    if assert_in("analyzer", resp, "Created analyzer element"):
        assert_equals("libreport", resp["analyzer"])

    tf.p2.DeleteProblems([pr_id])


def test_crash_signal(tf):
    print("TEST CRASH SIGNAL")

    if len(tf.crash_signal_occurrences) != 1:
        error("Crash signal wasn't emitted")
    else:
        if tf.crash_signal_occurrences[0][0] != tf.problem_id:
            error("Crash signal was emitted with wrong PATH")
        if tf.crash_signal_occurrences[0][1] != os.geteuid():
            error("Crash signal was emitted with wrong UID")


def test_get_problems(tf):
    print("TEST GET PROBLEMS")

    p = tf.p2.GetProblems()
    if not p:
        error("no problems")

    if not tf.problem_id in p:
        error("missing our problem")

    if tf.private_problem_id in p:
        error("contains private problem")


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
        "analyzer"    : (2, len("problems2testsuite_analyzer"), "problems2testsuite_analyzer"),
        "type"        : (2, len("problems2testsuite_type"), "problems2testsuite_type"),
        "reason"      : (2, len("Application has been killed"), "Application has been killed"),
        "backtrace"   : (2, len("die()"), "die()"),
        "executable"  : (2, len("/usr/bin/foo"), "/usr/bin/foo"),
        "hugetext"    : (64, os.path.getsize("/tmp/hugetext"), "/var/spool/abrt/[^/]+/hugetext"),
        "binary"      : (1, os.path.getsize("/usr/bin/true"), "/var/spool/abrt/[^/]+/binary"),
    }

    for k, v in expected.items():
        if not k in p:
            error("missing " + k)
            continue

        g = p[k]
        if not re.match(v[2], g[2]):
            error("invalid contents of '%s'" % (k))

        if g[1] != v[1]:
            error("invalid length '%s' : %i" % (k, g[1]))

        if (g[0] & v[0]) != v[0]:
            error("invalid flags %s : %i" % (k, g[0]))


def test_get_private_problem(tf):
    print("TEST GET PRIVATE PROBLEM")

    p = tf.p2.GetProblems()
    if not p:
        error("no problems")

    if not tf.problem_id in p:
        error("missing our problem")

    if not tf.private_problem_id in p:
        error("missing private problem")

    p = tf.p2.GetProblemData(tf.private_problem_id)

    if p["uid"][2] != "0":
        error("invalid UID")


def test_private_problem_not_accessible(tf):
    print("TEST PRIVATE PROBLEM NOT ACCESSIBLE WHEN SESSION IS CLOSED")

    p = tf.p2.GetProblems()
    if not p:
        error("no problems")

    if not tf.problem_id in p:
        error("missing our problem")

    if tf.private_problem_id in p:
        error("accessible private problem")

    expect_dbus_error(DBUS_ERROR_ACCESS_DENIED_READ,
            tf.p2.GetProblemData, tf.private_problem_id)

    expect_dbus_error(DBUS_ERROR_ACCESS_DENIED_DELETE,
            tf.p2.DeleteProblems, [tf.private_problem_id])


def test_delete_problems(tf):
    print("TEST DELETE PROBLEMS")

    description = {"analyzer"    : "problems2testsuite_analyzer",
                   "type"        : "problems2testsuite_type",
                   "reason"      : "Application has been killed",
                   "backtrace"   : "die()",
                   "executable"  : "/usr/bin/sh",
                   "duphash"     : None,
                   "uuid"        : None}

    description["duphash"] = description["uuid"] = "DEADBEEF"
    one = tf.p2.NewProblem(description)
    time.sleep(1)

    description["duphash"] = description["uuid"] = "81680083"
    two = tf.p2.NewProblem(description)
    time.sleep(1)

    description["duphash"] = description["uuid"] = "FFFFFFFF"
    three = tf.p2.NewProblem(description)
    time.sleep(1)

    p = tf.p2.GetProblems()
    if not(one in p and two in p and three in p):
        error("problems not detected")

    tf.p2.DeleteProblems([one])

    p = tf.p2.GetProblems()
    if one in p:
        error("'one' not removed")

    if not(two in p and three in p):
        error("'two' and 'three' disappeared")

    expect_dbus_error(DBUS_ERROR_BAD_ADDRESS,
            tf.p2.DeleteProblems, [two, three, one])

    p = tf.p2.GetProblems()
    if two in p and three in p:
        error("'two' and 'three' not removed")

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
    if not p2_session_obj.startswith("/org/freedesktop/Problems2/Session/"):
        error("strange session path : %s" % (str(p2_session_obj)))

    tf.p2_session_proxy = tf.bus.get_object(BUS_NAME, p2_session_obj)
    tf.p2_session_props = dbus.Interface(tf.p2_session_proxy, dbus_interface=dbus.PROPERTIES_IFACE)
    tf.p2_session = dbus.Interface(tf.p2_session_proxy, dbus_interface='org.freedesktop.Problems2.Session')

    if tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"):
        error("is authorized by default")


def test_authrorize(tf):
    print("TEST AUTHORIZE")

    tf.ac_signal_occurrences = []
    tf.p2_session.connect_to_signal("AuthorizationChanged", tf.handle_authorization_changed)

    with start_polkit_agent(tf.root_bus, tf.bus.get_unique_name()) as pk_agent:
        def check_pending_authorization(retval):
            logging.debug("Calling Authorize(): expecting pending")
            ret = tf.p2_session.Authorize(dict())
            assert_equals(2, ret, "Not-yet finished authorization request")
            tf.interrupt_waiting()
            return retval

        pk_agent.set_replies([partial(check_pending_authorization, False),
                              partial(check_pending_authorization, True)])

        logging.debug("Calling Authorize(): expecting failure")

        ret = tf.p2_session.Authorize(dict())
        assert_equals(1, ret, "Pending authorization request")

        tf.loop_counter += 1
        tf.wait_for_signals(["AuthorizationChanged"])

        if assert_true(len(tf.ac_signal_occurrences) == 1, "Pending signal wasn't emitted"):
            assert_equals(1, tf.ac_signal_occurrences[0], "Pending signal value")

        assert_true(not tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"),
                    "Pending authorization request made Session authorized")

        tf.wait_for_signals(["AuthorizationChanged"])

        if assert_true(len(tf.ac_signal_occurrences) == 2, "Failure signal wasn't emitted"):
            assert_equals(3, tf.ac_signal_occurrences[1], "Failure signal value")

        assert_true(not tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"),
                    "Failed authorization request made Session authorized")

        logging.debug("Calling Authorize(): expecting success")

        ret = tf.p2_session.Authorize(dict())
        assert_equals(1, ret, "Pending authorization request")

        tf.loop_counter += 1
        tf.wait_for_signals(["AuthorizationChanged"])

        if assert_true(len(tf.ac_signal_occurrences) == 3, "Pending signal 2 wasn't emitted"):
            assert_equals(1, tf.ac_signal_occurrences[2], "Pending signal 2 value")

        assert_true(not tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"),
                    "Pending authorization request 2 made Session authorized")

        tf.wait_for_signals(["AuthorizationChanged"])

        if assert_true(len(tf.ac_signal_occurrences) == 4, "Authorized signal wasn't emitted"):
            assert_equals(0, tf.ac_signal_occurrences[3], "Authorized signal value")

        assert_true(tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"),
                    "Authorization request did not make Session authorized")



def test_close(tf):
    print("TEST CLOSE")

    tf.p2_session.Close()

    tf.ac_signal_occurrences = []
    tf.wait_for_signals(["AuthorizationChanged"])

    if assert_true(len(tf.ac_signal_occurrences) == 1, "Closed session signal wasn't emitted"):
        assert_equals(2, tf.ac_signal_occurrences[0], "Closed session signal value")

    p2_session_obj = tf.p2.GetSession()
    tf.p2_session_proxy = tf.bus.get_object(BUS_NAME, p2_session_obj)
    tf.p2_session_props = dbus.Interface(tf.p2_session_proxy, dbus_interface=dbus.PROPERTIES_IFACE)
    tf.p2_session = dbus.Interface(tf.p2_session_proxy, dbus_interface='org.freedesktop.Problems2.Session')

    if tf.p2_session_props.Get(tf.p2_session.dbus_interface, "is_authorized"):
        error("still authorized")


def create_problem(p2, wait=True):
    randomstring = ''.join(random.SystemRandom().choice(string.ascii_uppercase + string.digits) for _ in range(16))
    description = {"analyzer"    : "problems2testsuite_analyzer",
                   "type"        : "problems2testsuite_type",
                   "reason"      : "Application has been killed",
                   "duphash"     : randomstring,
                   "uuid"        : randomstring,
                   "backtrace"   : "die()",
                   "executable"  : "/usr/bin/foo",}

    p2p = p2.NewProblem(description)

    if wait:
        time.sleep(1)

    return p2p


def test_problem_entry_properties(tf):
    print("TEST ELEMENTARY ENTRY PROPERTIES")

    p2e = Problems2Entry(tf.bus, tf.problem_id)

    if not re.match("/var/spool/abrt/problems2testsuite_type[^/]*", p2e.getproperty("id")):
        error("strange problem ID")

    assert_equals(pwd.getpwuid(os.geteuid()).pw_name, p2e.getproperty("user"), "User name")
    assert_equals(socket.gethostname(), p2e.getproperty("hostname"), "hostname")

    assert_equals("problems2testsuite_type", p2e.getproperty("type"), "type")
    assert_equals("/usr/bin/foo", p2e.getproperty("executable"), "executable")
    assert_equals("/usr/bin/foo --blah", p2e.getproperty("command_line_arguments"), "command_line_arguments")
    assert_equals("abrt", p2e.getproperty("component"), "component")
    assert_equals("FEDCBA9876543210", p2e.getproperty("duphash"), "duphash")
    assert_equals("0123456789ABCDEF", p2e.getproperty("uuid"), "uuid")
    assert_equals("Application has been killed", p2e.getproperty("reason"), "reason")
    assert_equals(os.geteuid(), p2e.getproperty("uid"), "uid")
    assert_equals(1, p2e.getproperty("count"), "count")
    assert_equals(p2e.getproperty("last_occurrence"), p2e.getproperty("first_occurrence"), "first_occurrence == last_occurrence")

    if abs(p2e.getproperty("first_occurrence") - tf.problem_first_occurrence) >= 5:
        error("too old first occurrence")

    if abs(p2e.getproperty("last_occurrence") - tf.problem_first_occurrence) >= 5:
        error("too old last occurrence")

    assert_equals(False, p2e.getproperty("is_reported"), "is_reported")
    assert_equals(True, p2e.getproperty("can_be_reported"), "can_be_reported")
    assert_equals(False, p2e.getproperty("is_remote"), "is_reported")

    package = p2e.getproperty("package")
    assert_equals(5, len(package), "insufficient number of package members")

    exp_package = ("problems2-1.2-3", "", "problems2", "1.2", "3")
    assert_equals(exp_package, package, "invalid package struct")

    elements = p2e.getproperty("elements")
    assert_not_equals(0, len(elements), "Number of elements")

    for e in ["analyzer", "type", "reason", "backtrace", "executable", "uuid",
              "duphash", "package", "pkg_name", "pkg_version", "pkg_release",
              "cmdline", "component", "hugetext", "binary", "count", "time"]:

        assert_in(e, elements, "Property elements")

    reports = p2e.getproperty("reports")
    assert_equals(3, len(reports), "missing reports")

    exp = [
        ("ABRT Server", { "BTHASH" : "0123456789ABCDEF", "MSG" : "test"}),
        ("Server", { "URL" : "http://example.org"}),
        ("Server", { "URL" : "http://case.org"}),
        ]

    for i in range(0, len(exp) - 1):
        assert_equals(exp[i][0], reports[i][0], "invalid reported_to label")
        assert_equals(exp[i][1], reports[i][1], "invalid reported_to value")


def test_read_elements(tf):
    print("TEST READ ELEMENTS")

    requested = { "reason" : dbus.types.String,
                  "hugetext" : dbus.types.UnixFd,
                  "binary" : dbus.types.UnixFd }

    p2e = Problems2Entry(tf.bus, tf.problem_id)
    elements = p2e.ReadElements(requested.keys(), 0)

    for r, t in requested.items():
        if not r in elements:
            error("response is missing %s" % (r))
            continue

        if type(elements[r]) != t:
            error("invalid type of %s: %s" % (r, str(type(elements[r]))))

    resp = p2e.ReadElements([], 0x0)
    if len(resp) != 0:
        error("the response for an empty request is not empty")

    resp = p2e.ReadElements(["foo"], 0x0)
    if len(resp) != 0:
        error("the response for an request with non-existing element is not empty")

    resp = p2e.ReadElements(["/etc/shadow", "../../../../etc/shadow"], 0x0)
    if len(resp) != 0:
        error("the response for an request with prohibited elements is not empty")

    reasonlist = p2e.ReadElements(["reason"], 0x08)
    if reasonlist:
        error("returned text when ONLY_BIG_TEXT requested")

    reasonlist = p2e.ReadElements(["reason"], 0x10)
    if reasonlist:
        error("returned text when ONLY_BIN requested")

    reasonlist = p2e.ReadElements(["reason"], 0x04)
    if len(reasonlist) != 1:
        error("not returned text when ONLY_TEXT requested")

    if reasonlist["reason"] != "Application has been killed":
        error("invalid data returned")

    reasonlist = p2e.ReadElements(["reason"], 0x01 | 0x04)
    if len(reasonlist) != 1:
        error("not returned fd when ALL_FD | ONLY_TEXT requested")

    fd = reasonlist["reason"].take()

    # try read few more bytes to verify that the file is not broken
    data = os.read(fd, len("Application has been killed") + 10)
    if "Application has been killed" != data.decode():
        error("invalid data read from file descriptor : '%s'" % (data))
    os.close(fd)


def test_save_elements(tf):
    print("TEST SAVE ELEMENTS")

    p2e = Problems2Entry(tf.bus, tf.problem_id)

    shorttext = "line one\nline two\nline three\n"
    with open("/tmp/shorttext", "w") as shorttext_file:
        shorttext_file.write(shorttext)

    with open("/tmp/shorttext", "r") as fstab_file:
        request = { "random" : "random text",
                    "shorttext" : dbus.types.UnixFd(fstab_file) }

        dummy = p2e.SaveElements(request, 0)

    elements = p2e.getproperty("elements")
    if not "random" in elements or not "shorttext" in elements:
        error("property 'elements' does not include the created elements")

    resp = p2e.ReadElements(["random", "shorttext"], 0x00)
    if len(resp) != 2:
        error("not returned both requested elements")

    dictionary_key_has_value(resp, "random", "random text")
    dictionary_key_has_value(resp, "shorttext", shorttext)

    resp = p2e.SaveElements(dict(), 0x0)

    for path in ["/tmp/shadow", "/tmp/passwd"]:
        try:
            os.unlink(path)
        except OSError:
            pass

    expect_dbus_error("org.freedesktop.DBus.Error.AccessDenied: Not allowed problem element name",
            p2e.SaveElements, {"/tmp/shadow" : "blah"}, 0x0)

    try:
        os.unlink("/tmp/shadow")
        error("accepted an absolute path")
    except OSError:
        pass

    expect_dbus_error("org.freedesktop.DBus.Error.AccessDenied: Not allowed problem element name",
            p2e.SaveElements, {"../../../../tmp/passwd" : "bar"}, 0x0)

    try:
        os.unlink("/tmp/passwd")
        error("accepted a relative path")
    except OSError:
        pass


def test_delete_elements(tf):
    print("TEST DELETE ELEMENTS")

    p2e = Problems2Entry(tf.bus, tf.problem_id)

    deleted_elements = { "delete_one" : "delete one",
                         "delete_two" : "delete two",
                         "delete_six" : "delete six" }

    p2e.SaveElements(deleted_elements, 0x0)
    elements = p2e.getproperty("elements")
    for e in deleted_elements.keys():
        if not e in elements:
            error("element does not exist: %s" % (e))

    p2e.DeleteElements(["delete_one"])
    elements = p2e.getproperty("elements")
    if "delete_one" in elements:
        error("'delete_one' has not been removed")
    if not "delete_two" in elements or not "delete_six" in elements:
        error("the other elements have disappeared")

    p2e.DeleteElements(["delete_one", "delete_two", "delete_six"])
    elements = p2e.getproperty("elements")
    if "delete_two" in elements or "delete_six" in elements:
        error("the other elements have not been removed")

    p2e.DeleteElements([])

    for path in ["/tmp/shadow", "/tmp/passwd"]:
        with open(path, "w") as tmp_file:
            tmp_file.write("should not be touched")

    resp = p2e.DeleteElements(["/tmp/shadow", "../../../../tmp/passwd"])

    try:
        os.unlink("/tmp/shadow")
    except OSError as ex:
        error("removed an absolute path: %s" % (str(ex)))

    try:
        os.unlink("/tmp/passwd")
    except OSError as ex:
        error("removed a relative path: %s" % (str(ex)))


def test_open_too_many_sessions(tf):
    print("TEST LIMIT OF OPENED SESSIONS")

    sessions = dict()
    i = 0
    try:
        while i < 10 :
            bus = dbus.SystemBus(private=True)
            p2_proxy = bus.get_object(BUS_NAME, '/org/freedesktop/Problems2')
            p2 = dbus.Interface(p2_proxy, dbus_interface='org.freedesktop.Problems2')
            p2s_path = p2.GetSession()
            i += 1
            if p2s_path in sessions:
                error("got a session owned by another caller, run = %d" % (i))
            else:
                sessions[p2s_path] = (bus, p2_proxy, p2)
        error("managed to open %d sessions" % (i))
    except dbus.exceptions.DBusException as ex:
        assert_equals("org.freedesktop.DBus.Error.Failed: Too many sessions opened", str(ex), "managed to open %d sessions" % (i))
        # one session is already opened and 5 is the limit
        assert_equals(4, i, "unexpected opened sessions limit")

    for k, v in sessions.items():
        p2s_proxy = v[0].get_object(BUS_NAME, k)
        p2s = dbus.Interface(p2s_proxy, dbus_interface='org.freedesktop.Problems2.Session')
        p2s.Close()


def test_foreign_session(tf):
    print("TEST FOREIGN SESSION")

    root_session_proxy = tf.bus.get_object(BUS_NAME, tf.root_p2.GetSession())
    root_session = dbus.Interface(root_session_proxy, dbus_interface='org.freedesktop.Problems2.Session')

    expect_dbus_error("org.freedesktop.DBus.Error.Failed: Your Problems2 Session is broken. Check system logs for more details.",
            root_session.Close)


def test_new_problem_elements_count_limit(tf):
    print("TEST NEW PROBLEM ELEMENTS COUNT LIMIT")

    too_many_elements = dict()
    for i in range(DBUS_LIMIT_ELEMENTS_COUNT + 1):
        too_many_elements[str(i)] = str(i)

    status, pp = expect_dbus_error("org.freedesktop.DBus.Error.LimitsExceeded: Too many elements",
                    tf.p2.NewProblem, too_many_elements)

    if not assert_true(status, "NewProblem returns an error for too many elements"):
        try:
            tf.p2.DeleteProblems([pp])
        except dbus.exceptions.DBusException as ex:
            print("Failed to remove elements count test problem: %s" % (str(ex)))


def test_save_elements_count_limit(tf):
    print("TEST SAVE ELEMENTS COUNT LIMIT")

    problem_path = create_problem(tf.p2)
    entry = Problems2Entry(tf.bus, problem_path)

    too_many_elements = dict()
    for i in range(DBUS_LIMIT_ELEMENTS_COUNT + 1):
        too_many_elements[str(i)] = str(i)

    entry.SaveElements(too_many_elements, 0)
    elements = entry.getproperty("elements")
    saved = False
    ignored = False
    for i in range(DBUS_LIMIT_ELEMENTS_COUNT + 1):
        if str(i) in elements:
            saved = True
        else:
            ignored = True

    assert_true(saved, "SaveElements: saved as many as possible")
    assert_true(ignored, "SaveElements: did not save elements over limit")

    for i in range(DBUS_LIMIT_ELEMENTS_COUNT + 1):
        key = str(DBUS_LIMIT_ELEMENTS_COUNT + 1 + i)
        ed = { key : str(i + 1) }
        try:
            entry.SaveElements(ed, 0)
            data = entry.ReadElements([key], 0)
        except dbus.exceptions.DBusException as ex:
            print (key)
            raise ex
        assert_not_in(key, data, "SaveElements: did not create new elements")

    overwrites = False
    for i in range(DBUS_LIMIT_ELEMENTS_COUNT + 1):
        ed = { str(i) : str(i + 2) }
        try:
            entry.SaveElements(ed, 0)
            overwrites = True
        except dbus.exceptions.DBusException as ex:
            if not assert_equals("org.freedesktop.DBus.Error.LimitsExceeded: Too many elements", str(ex)):
                break

    assert_true(overwrites, "SaveElements allows to overwrite element despite Elements count limit")

    tf.p2.DeleteProblems([problem_path])


def get_huge_file_path(tf):
    huge_file_path = "/var/tmp/abrt.testsuite.huge-file"
    try:
        size = os.path.getsize(huge_file_path)
        if size < DBUS_LIMIT_DATA_SIZE + 1024:
            raise OSError
    except OSError:
        subprocess.call(['dd', 'bs=1024', 'count=' + str(int(DBUS_LIMIT_DATA_SIZE/1024) + 1), 'if=/dev/urandom', 'of=' + huge_file_path])

    return huge_file_path


def test_new_problem_data_size_limit(tf):
    print("TEST NEW PROBLEM DATA SIZE LIMIT")

    huge_file_path = get_huge_file_path(tf)

    with open(huge_file_path, "r") as huge_file:
        description = {"analyzer"    : "problems2testsuite_analyzer",
                       "reason"      : "Application has been killed",
                       "backtrace"   : "die()",
                       "duphash"     : "FAKE_BINARY_TYPE",
                       "uuid"        : "FAKE_BINARY_TYPE",
                       "huge_file"   : dbus.types.UnixFd(huge_file),
                       "executable"  : "/usr/bin/foo",
                       "type"        : "abrt-problems2-sanity"}

        expect_dbus_error("org.freedesktop.DBus.Error.LimitsExceeded: Problem data is too big",
                              tf.p2.NewProblem, description)


def test_save_elements_data_size_limit(tf):
    print("TEST SAVE ELEMENTS DATA SIZE LIMIT")

    huge_file_path = get_huge_file_path(tf)

    problem_path = create_problem(tf.p2)
    entry = Problems2Entry(tf.bus, problem_path)

    key = "huge_file"
    with open(huge_file_path, "r") as huge_file:
        entry.SaveElements({key : dbus.types.UnixFd(huge_file)}, 0)
        data = entry.ReadElements([key], 0x1)
        if assert_in(key, data, "SaveElements: created truncated file"):
            fd = data[key].take()
            end = os.lseek(fd, 0, os.SEEK_END)
            os.close(fd)
            assert_lower_than(DBUS_LIMIT_DATA_SIZE, end, "SaveElements: wrote up to Size limit Bytes")

    smaller_ed = {key : "smaller file"}
    entry.SaveElements(smaller_ed, 0)
    data = entry.ReadElements([key], 0x4)
    if assert_in(key, data, "SaveElements: created non-text file"):
        assert_equals(smaller_ed[key], data[key], "SaveElements: dump directory does not grow")


def test_new_problem_time_limit(tf):
    print("TEST NEW PROBLEMS COUNT PER PERIOD OF TIME LIMIT")

    problems = list()
    def bunch_of_new_problems(upper_limit):
        for i in range(0, upper_limit):
            problem_path = create_problem(tf.p2, wait=False)
            problems.append(problem_path)

    expect_dbus_error("org.freedesktop.DBus.Error.LimitsExceeded: Too many problems have been recently created",
           bunch_of_new_problems, 11)

    tf.p2.DeleteProblems(problems)
    problems = list()

    time.sleep(16)

    expect_dbus_error("org.freedesktop.DBus.Error.LimitsExceeded: Too many problems have been recently created",
           bunch_of_new_problems, 3)

    assert_true(len(problems) >= 1, "The limit has been restored")
    tf.p2.DeleteProblems(problems)


if __name__ == "__main__":
    if os.getuid() != 0:
        print("Run this test under root!")
        sys.exit(1)

    if len(sys.argv) != 2:
        print("Pass an uid of non-root user as the first argument!")
        sys.exit(1)

    #logging.getLogger().setLevel(logging.DEBUG)

    non_root_uid = int(sys.argv[1])
    test_frame = TestFrame(non_root_uid)

    test_fake_binary_type(test_frame)
    test_not_allowed_elements(test_frame)

    test_frame.crash_signal_occurrences = []
    test_frame.p2.connect_to_signal("Crash", test_frame.handle_crash)

    test_new_problem_sanitized_uid(test_frame)
    test_new_problem_sanitized_elements(test_frame)

    test_real_problem(test_frame)
    test_frame.wait_for_signals(["Crash"])

    test_frame.private_problem_id = create_problem(test_frame.root_p2)

    test_get_problems(test_frame)
    test_get_problem_data(test_frame)
    test_delete_problems(test_frame)

    # Would be better to restart the service!
    time.sleep(16)

    test_get_session(test_frame)

    test_authrorize(test_frame)

    test_get_private_problem(test_frame)

    test_close(test_frame)

    test_private_problem_not_accessible(test_frame)
    test_problem_entry_properties(test_frame)
    test_read_elements(test_frame)
    test_save_elements(test_frame)
    test_delete_elements(test_frame)

    test_open_too_many_sessions(test_frame)
    test_foreign_session(test_frame)

    test_new_problem_elements_count_limit(test_frame)
    test_save_elements_count_limit(test_frame)

    test_new_problem_data_size_limit(test_frame)
    test_save_elements_data_size_limit(test_frame)

    test_new_problem_time_limit(test_frame)

    # Don't test it until the limit can be configured
    # test_problems_count_limit(test_frame)

    test_frame.p2_session.Close()

    rsp = test_frame.root_bus.get_object(BUS_NAME, test_frame.root_p2.GetSession())
    rs = dbus.Interface(rsp, dbus_interface='org.freedesktop.Problems2.Session')
    rs.Close()
