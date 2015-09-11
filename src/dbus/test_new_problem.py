#!/usr/bin/python3
import os
import dbus

bus = dbus.SystemBus()
proxy = bus.get_object("org.freedesktop.problems",
                       '/org/freedesktop/Problems2')

problems = dbus.Interface(proxy, dbus_interface='org.freedesktop.Problems2')

with open("/tmp/fake_type", "w") as type_file:
    type_file.write("CCpp")

with open("/tmp/fake_type", "r") as type_file:
    description = {"analyzer"    : "libreport",
                   "reason"      : "Application has been killed",
                   "backtrace"   : "die()",
                   "executable"  : "/usr/bin/foo",
                   "type"        : dbus.types.UnixFd(type_file)}

    try:
        problems.NewProblem(description)
        print("FAILURE : an exception expected")
    except dbus.exceptions.DBusException as ex:
        if str(ex) != "org.freedesktop.problems.Failure: You are not allowed to create element 'type' containing 'CCpp'":
            print("FAILURE : wrong message : %s" % (str(ex)))

with open("/etc/services", "r") as services_file:
    description = {"analyzer"    : "libreport",
                   "type"        : "libreport",
                   "reason"      : "Application has been killed",
                   "backtrace"   : "die()",
                   "executable"  : "/usr/bin/foo",
                   "services"    : dbus.types.UnixFd(services_file)}

    p = problems.NewProblem(description)
    if not p:
        print("FAILURE : empty return value")
