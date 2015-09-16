sbin_PROGRAMS = \
    abrt-dbus \
    abrt-configuration \
    abrt-problems2

abrt_dbus_SOURCES = \
    abrt-dbus.c \
    abrt-polkit.c \
    abrt-polkit.h
abrt_dbus_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DVAR_RUN=\"$(VAR_RUN)\" \
    $(GIO_CFLAGS) \
    $(DBUS_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    $(POLKIT_CFLAGS) \
    -D_GNU_SOURCE
abrt_dbus_LDADD = \
    $(GIO_LIBS) \
    ../lib/libabrt.la \
    $(LIBREPORT_LIBS) \
    $(POLKIT_LIBS) \
    -labrt_dbus

# TODO abrt-polkit to noninst library
abrt_problems2_SOURCES = \
    abrt-polkit.c \
    abrt-polkit.h \
    abrt_problems2_service.c \
    abrt_problems2_service.h \
    abrt_problems2_node.c \
    abrt_problems2_node.h \
    abrt_problems2_session_node.c \
    abrt_problems2_session_node.h \
    abrt_problems2_entry_node.c \
    abrt_problems2_entry_node.h

abrt_problems2-abrt_problems2_service.$(OBJEXT):  abrt_problems2_generated_interfaces.h

abrt_problems2_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DVAR_RUN=\"$(VAR_RUN)\" \
    $(GIO_CFLAGS) \
    $(GIO_UNIX_CFLAGS) \
    $(DBUS_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    $(POLKIT_CFLAGS) \
    -D_GNU_SOURCE
abrt_problems2_LDADD = \
    $(GIO_LIBS) \
    $(GIO_UNIX_LIBS) \
    ../lib/libabrt.la \
    $(LIBREPORT_LIBS) \
    $(POLKIT_LIBS) \
    -labrt_dbus

abrt_configuration_SOURCES = \
    abrt-configuration.c \
    abrt-polkit.c \
    abrt-polkit.h
abrt_configuration_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DVAR_RUN=\"$(VAR_RUN)\" \
    -DPROBLEMS_CONFIG_INTERFACES_DIR=\"$(PROBLEMS_CONFIG_INTERFACES_DIR)\" \
    $(GIO_CFLAGS) \
    $(DBUS_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    $(POLKIT_CFLAGS) \
    -D_GNU_SOURCE
abrt_configuration_LDADD = \
    $(GIO_LIBS) \
    ../lib/libabrt.la \
    $(LIBREPORT_LIBS) \
    $(POLKIT_LIBS)

DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

polkitconfdir = ${datadir}/polkit-1/actions
dist_polkitconf_DATA = abrt_polkit.policy

dbusabrtconfdir = ${sysconfdir}/dbus-1/system.d/
dist_dbusabrtconf_DATA = dbus-abrt.conf

orgfreedesktopproblemsservicedir = ${datadir}/dbus-1/system-services
dist_orgfreedesktopproblemsservice_DATA = org.freedesktop.problems.service \
    com.redhat.problems.configuration.service

iface_def_dir=$(abs_top_srcdir)/doc/problems-service/

.PHONY: $(iface_def_dir)/org.freedesktop.Problems2.xml
$(iface_def_dir)/org.freedesktop.Problems2.xml:
	$(MAKE) -C $(iface_def_dir) $$(basename $@)

.PHONY: $(iface_def_dir)/org.freedesktop.Problems2.Entry.xml
$(iface_def_dir)/org.freedesktop.Problems2.Entry.xml:
	$(MAKE) -C $(iface_def_dir) $$(basename $@)

.PHONY: $(iface_def_dir)/org.freedesktop.Problems2.Session.xml
$(iface_def_dir)/org.freedesktop.Problems2.Session.xml:
	$(MAKE) -C $(iface_def_dir) $$(basename $@)

.PHONY: abrt_problems2_generated_interfaces.h
abrt_problems2_generated_interfaces.h: $(iface_def_dir)/org.freedesktop.Problems2.xml $(iface_def_dir)/org.freedesktop.Problems2.Entry.xml $(iface_def_dir)/org.freedesktop.Problems2.Session.xml
	echo "/* This file has been generated. Do not write any code here. */" > $@
	for f in $^; do echo "const char *g_$$(basename $$f .xml | tr . _)_xml =" >> $@; sed -e 's/"/\\"/g' -e 's/^\(.*\)$$/"\1"/' $$f >> $@; echo ";" >> $@; done

eventsconfdir = $(EVENTS_CONF_DIR)
