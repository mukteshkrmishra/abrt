#!/usr/bin/python3

import dbus

import abrt_p2_testing
from abrt_p2_testing import BUS_NAME

class TestSessionLimits(abrt_p2_testing.TestCase):

    def test_open_too_many_sessions(self):
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
                    self.fail("got a session owned by another caller, run = %d" % (i))
                else:
                    sessions[p2s_path] = (bus, p2_proxy, p2)
            self.fail("managed to open %d sessions" % (i))
        except dbus.exceptions.DBusException as ex:
            self.assertEqual("org.freedesktop.DBus.Error.Failed: Too many sessions opened", str(ex), "managed to open %d sessions" % (i))
            self.assertEqual(5, i, "unexpected opened sessions limit")

        for k, v in sessions.items():
            p2s_proxy = v[0].get_object(BUS_NAME, k)
            p2s = dbus.Interface(p2s_proxy, dbus_interface='org.freedesktop.Problems2.Session')
            p2s.Close()

    def test_foreign_session(self):
        root_session_path = self.root_p2.GetSession()

        root_session_proxy = self.bus.get_object(BUS_NAME, root_session_path)
        root_session = dbus.Interface(root_session_proxy, dbus_interface='org.freedesktop.Problems2.Session')

        self.assertRaisesDBusError("org.freedesktop.DBus.Error.Failed: Your Problems2 Session is broken. Check system logs for more details.",
                    root_session.Close)

        root_session_proxy = self.root_bus.get_object(BUS_NAME, root_session_path)
        root_session = dbus.Interface(root_session_proxy, dbus_interface='org.freedesktop.Problems2.Session')
        root_session.Close()


if __name__ == "__main__":
    abrt_p2_testing.main(TestSessionLimits)
