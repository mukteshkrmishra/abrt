#!/bin/bash
# vim: dict=/usr/share/beakerlib/dictionary.vim cpt=.,w,b,u,t,i,k
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
#   runtest.sh of dbus-problems2-sanity
#   Description: Check Problems2 D-Bus API
#   Author: Jakub Filak <jfilak@redhat.com>
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
#   Copyright (c) 2015 Red Hat, Inc. All rights reserved.
#
#   This program is free software: you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation, either version 3 of
#   the License, or (at your option) any later version.
#
#   This program is distributed in the hope that it will be
#   useful, but WITHOUT ANY WARRANTY; without even the implied
#   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
#   PURPOSE.  See the GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program. If not, see http://www.gnu.org/licenses/.
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
#   If you want to test development version of abrt-dbus run
#   the following command:
#
#     $ sudo PATH=$ADD_YOUR_PATH/abrt/src/dbus:$PATH ./runtest.sh
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

. /usr/share/beakerlib/beakerlib.sh
. ../aux/lib.sh

TEST="dbus-problems2-sanity"
PACKAGE="abrt-dbus"
TEST_USER="abrt-dbus-test"

rlJournalStart
    rlPhaseStartSetup
        check_prior_crashes

        TmpDir=$(mktemp -d)
        cp -r ./cases $TmpDir
        pushd $TmpDir

        useradd $TEST_USER -M -g wheel || rlDie "Cannot proceed without the user"
        echo "kokotice" | passwd $TEST_USER --stdin || {
            userdel -r -f $TEST_USER
            rlDie "Failed to update password"
        }

        TEST_USER_UID=$(id -u $TEST_USER | tr -d "\n")

        killall abrt-dbus
    rlPhaseEnd

    rlPhaseStartTest
        for test_fixture in `ls cases/test_*.py`
        do
            rlLog "`which abrt-dbus`"
            abrt-dbus -vvv -t 100 &> abrt_dbus_${test_fixture#cases/}.log &
            ABRT_DBUS_PID=$!

            rlRun "python3 $test_fixture $TEST_USER_UID"

            kill $ABRT_DBUS_PID
        done
    rlPhaseEnd

    rlPhaseStartCleanup
        rlBundleLogs abrt $(ls *.log)

        userdel -r -f $TEST_USER

        popd # TmpDir
        rm -rf $TmpDir
    rlPhaseEnd
    rlJournalPrintText
rlJournalEnd
