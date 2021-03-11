#!/bin/bash

set -x -e -o pipefail

# contains information about this centos release
. /etc/os-release
export CODENAME=${ID}_${VERSION_ID}

yum install -y epel-release

# why does centos7 not have shunit2 packages?
yum install -y https://kojipkgs.fedoraproject.org/packages/shunit2/2.1.6/16.fc30/noarch/shunit2-2.1.6-16.fc30.noarch.rpm

yum install -y packages/${DIRNAME}/amplet2-client-*.rpm

# TODO is there something smarter we can use like autopkgtest in debian?
# run all the tests in the test directory
set +x
summary=""
failed=0
for t in tests/test_*sh; do
    if [ "$t" = "tests/test_helper.sh" ]; then
        continue
    fi

    if "$t"; then
        result="$t\t\tPASS"
        summary="$summary\n$result"
        echo -e "$result"
    else
        failed=$(($failed + 1))
        result="$t\t\tFAIL"
        summary="$summary\n$result"
        echo -e "$result"
    fi
done

# print a summary that looks vaguely like autopkgtest
echo "@@@@@@@@@@@@@@@@@@@@ summary"
echo -e $summary | column -t

exit $failed
