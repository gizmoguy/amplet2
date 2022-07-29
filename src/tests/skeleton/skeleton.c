/*
 * This file is part of amplet2.
 *
 * Copyright (c) 2013-2016 The University of Waikato, Hamilton, New Zealand.
 *
 * Author: Brendon Jones
 *
 * All rights reserved.
 *
 * This code has been developed by the University of Waikato WAND
 * research group. For further information please see http://www.wand.net.nz/
 *
 * amplet2 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations including
 * the two.
 *
 * You must obey the GNU General Public License in all respects for all
 * of the code used other than OpenSSL. If you modify file(s) with this
 * exception, you may extend this exception to your version of the
 * file(s), but you are not obligated to do so. If you do not wish to do
 * so, delete this exception statement from your version. If you delete
 * this exception statement from all source files in the program, then
 * also delete it here.
 *
 * amplet2 is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with amplet2. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Skeleton test to demonstrate how to write a test for amplet2.
 */
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

#include "tests.h"
#include "debug.h"
#include "testlib.h"
#include "skeleton.pb-c.h"

amp_test_result_t* run_skeleton(int argc, char *argv[], int count,
        struct addrinfo **dests);
void print_skeleton(amp_test_result_t *result);
test_t *register_test(void);



static void usage(char *prog) {
    fprintf(stderr, "%s test has no required arguments.\n", prog);
    fprintf(stderr, "Any arguments it receives it will simply print.\n");
}



/*
 * Build the protocol buffer message containing the result.
 */
static amp_test_result_t* report_result(struct timeval *start_time,
        uint32_t valid) {

    amp_test_result_t *result = calloc(1, sizeof(amp_test_result_t));

    Log(LOG_DEBUG, "Building skeleton report, valid:%d\n", valid);

    Amplet2__Skeleton__Report msg = AMPLET2__SKELETON__REPORT__INIT;
    Amplet2__Skeleton__Header header = AMPLET2__SKELETON__HEADER__INIT;

    header.has_valid = 1;
    header.valid = valid;

    msg.header = &header;

    /* pack all the results into a buffer for transmitting */
    result->timestamp = (uint64_t)start_time->tv_sec;
    result->len = amplet2__skeleton__report__get_packed_size(&msg);
    result->data = malloc(result->len);
    amplet2__skeleton__report__pack(&msg, result->data);

    return result;
}


/*
 * Very simple main function to show how tests can be run.
 */
amp_test_result_t* run_skeleton(int argc, char *argv[], int count,
        struct addrinfo **dests) {
    int i;
    char address[INET6_ADDRSTRLEN];
    struct timeval start_time;
    uint32_t valid;
    amp_test_result_t *result;
    int opt;

    printf("skeleton test\n");

    /* use getopt to check for -h first, then fall through to dump all args */
    while ( (opt = getopt(argc, argv, "h")) != -1 ) {
	switch ( opt ) {
	    case 'h': usage(argv[0]); exit(EXIT_SUCCESS);
            default: /* pass through */ break;
	};
    }

    if ( gettimeofday(&start_time, NULL) != 0 ) {
	Log(LOG_ERR, "Could not gettimeofday(), aborting test");
	exit(EXIT_FAILURE);
    }

    /* print all the arguments that were passed in */
    printf("args:\n");
    for ( i=0; i<argc; i++) {
	printf("\targv[%d]: %s\n", i, argv[i]);
    }

    /* print all the destinations that were passed in */
    printf("dests: %d\n", count);
    valid = count;
    for ( i=0; i<count; i++ ) {
	if ( dests[i]->ai_family == AF_INET ) {
	    inet_ntop(AF_INET,
		    &((struct sockaddr_in*)dests[i]->ai_addr)->sin_addr,
		    address, INET6_ADDRSTRLEN);
	} else if ( dests[i]->ai_family == AF_INET6 ) {
	    inet_ntop(AF_INET6,
		    &((struct sockaddr_in6*)dests[i]->ai_addr)->sin6_addr,
		    address, INET6_ADDRSTRLEN);
	} else {
            valid--;
	    continue;
	}
	printf("\t%s\n", address);
    }

    /* report some sort of dummy result */
    result = report_result(&start_time, valid);

    return result;
}



/*
 * Unpack the protocol buffer object and print the results of the skeleton test.
 */
void print_skeleton(amp_test_result_t *result) {
    Amplet2__Skeleton__Report *msg;

    assert(result);
    assert(result->data);

    /* unpack all the data */
    msg = amplet2__skeleton__report__unpack(NULL, result->len, result->data);

    assert(msg);
    assert(msg->header);

    printf("Result: got %d address(es) of known family (IPv4/IPv6)\n",
            msg->header->valid);

    amplet2__skeleton__report__free_unpacked(msg, NULL);
}



/*
 * Register a test to be part of AMP.
 */
test_t *register_test() {
    test_t *new_test = (test_t *)malloc(sizeof(test_t));

    /* the test id is defined by the enum in tests.h */
    new_test->id = AMP_TEST_SKELETON;

    /* name is used to schedule the test and report results */
    new_test->name = strdup("skeleton");

    /* how many targets a single instance of this test can have */
    new_test->max_targets = 10;

    /* minimum number of targets required to run this test */
    new_test->min_targets = 1;

    /* maximum duration this test should take before being killed */
    new_test->max_duration = 30;

    /* function to call to setup arguments and run the test */
    new_test->run_callback = run_skeleton;

    /* function to call to pretty print the results of the test */
    new_test->print_callback = print_skeleton;

    /* the skeleton test doesn't require us to run a custom server */
    new_test->server_callback = NULL;

    /* don't give the skeleton test a SIGINT warning */
    new_test->sigint = 0;

    /* resolve targets before passing them to the test */
    new_test->do_resolve = 1;

    return new_test;
}
