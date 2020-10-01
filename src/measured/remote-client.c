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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#if ! _WIN32
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "debug.h"
#include "control.h"
#include "serverlib.h"
#include "ssl.h"
#include "measured.pb-c.h"
#include "controlmsg.h"
#include "modules.h"


struct option long_options[] = {
    {"cacert", required_argument, 0, '0'},
    {"cert", required_argument, 0, '9'},
    {"key", required_argument, 0, '8'},
    {"connect", required_argument, 0, 'c'},
    {"port", required_argument, 0, 'p'},
    {"test", required_argument, 0, 't'},
    {"list", required_argument, 0, 'l'},
    {"args", required_argument, 0, 'a'},
    {"debug", no_argument, 0, 'x'},
    {"help", no_argument, 0, 'h'},
    {NULL, 0, 0, 0}
};

static void usage(char *prog) {
    printf("usage: %s -c <host> -t <test> -- <target>\n", prog);
    printf("  --connect, -c <host>       host running amplet2-client to connect to\n");
    printf("  --port, -p    <port>       port to connect to (default %s)\n",
            DEFAULT_AMPLET_CONTROL_PORT);
    printf("  --test, -t    <test>       test to run\n");
    printf("  --list, -l                 list all available tests\n");
    printf("  --args, -a    <arg string> quoted string of test arguments\n");
    printf("  --cacert      <file>       PEM format CA certificate\n");
    printf("  --cert        <file>       PEM format certificate\n");
    printf("  --key         <file>       PEM format private key\n");
}



/*
 * List all tests available to this (local) client. Not ideal because it
 * doesn't know what tests are available on the remote client, but it's a
 * start.
 */
static void list_all_tests(void) {
    test_t **test;

    printf("Available tests (%s)\n", AMP_TEST_DIRECTORY);

    if ( amp_tests == NULL ) {
        printf("  none\n");
        return;
    }

    for ( test = amp_tests; *test != NULL; test++ ) {
        printf("  %s\n", (*test)->name);
    }
}



/*
 * Run a test on a remote amplet2 client and output the results locally.
 */
int main(int argc, char *argv[]) {
    int len;
    void *buffer;
    int bytes;
    BIO *ctrl;
    amp_ssl_opt_t sslopts;
    struct addrinfo hints, *dest;
    Amplet2__Measured__Control out_msg = AMPLET2__MEASURED__CONTROL__INIT;
    Amplet2__Measured__Test test_msg = AMPLET2__MEASURED__TEST__INIT;
    Amplet2__Measured__Response response;
    Amplet2__Measured__Control *in_msg;

    int i;
    int opt;
    char *test_name = NULL;
    test_t *test;
    char *host = "localhost";
    char *port = DEFAULT_AMPLET_CONTROL_PORT;
    char *args = NULL;
    struct sockopt_t sockopts;
    int list = 0;

    memset(&sockopts, 0, sizeof(sockopts));

    /* quieten down log messages while starting, we don't need to see them */
    log_level = LOG_WARNING;

    while ( (opt = getopt_long(argc, argv, "?h0:9:8:a:c:lp:t:x4:6:I:",
                    long_options, NULL)) != -1 ) {
        switch ( opt ) {
            case '0': sslopts.cacert = optarg; break;
            case '9': sslopts.cert = optarg; break;
            case '8': sslopts.key = optarg; break;
            case 'a': args = optarg; break;
            case 'c': host = optarg; break;
            case 'l': list = 1; break;
            case 'p': port = optarg; break;
            case 't': test_name = optarg; break;
            case 'x': log_level = LOG_DEBUG; break;
            case '4': sockopts.sourcev4 = get_numeric_address(optarg, NULL);
                      break;
            case '6': sockopts.sourcev6 = get_numeric_address(optarg, NULL);
                      break;
            case 'I': sockopts.device = optarg; break;
            case 'h':
            case '?': usage(argv[0]); exit(EXIT_SUCCESS);
            default: usage(argv[0]); exit(EXIT_FAILURE);
        };
    }

    /* register tests after setting log_level, just in case it's useful */
    register_tests(AMP_TEST_DIRECTORY);

    if ( list ) {
        list_all_tests();
        exit(EXIT_SUCCESS);
    }

    if ( test_name == NULL ) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if ( (test = get_test_by_name(test_name)) == NULL ) {
        printf("Invalid test: %s\n", test_name);
        list_all_tests();
        exit(EXIT_FAILURE);
    }

    if ( initialise_ssl(&sslopts, NULL) < 0 ) {
        exit(EXIT_FAILURE);
    }

    if ( (ssl_ctx = initialise_ssl_context(&sslopts)) == NULL ) {
        exit(EXIT_FAILURE);
    }

    /* build test schedule item */
    test_msg.has_test_type = 1;
    test_msg.test_type = test->id;
    test_msg.params = args;
    test_msg.n_targets = argc - optind;
    test_msg.targets = calloc(test_msg.n_targets, sizeof(char*));
    for ( i = optind; i < argc; i++ ) {
        test_msg.targets[i - optind] = argv[i];
    }

    out_msg.test = &test_msg;
    out_msg.has_type = 1;
    out_msg.type = AMPLET2__MEASURED__CONTROL__TYPE__TEST;

    len = amplet2__measured__control__get_packed_size(&out_msg);
    buffer = malloc(len);
    amplet2__measured__control__pack(&out_msg, buffer);

    free(test_msg.targets);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    getaddrinfo(host, port, &hints, &dest);

    /* connect to the control server, doing all the SSL establishment etc */
    if ( (ctrl = connect_control_server(dest, atoi(port),
                    &sockopts)) == NULL ) {
        printf("failed to connect control server\n");
        exit(EXIT_FAILURE);
    }

    /* send the test and arguments to the server */
    if ( write_control_packet(ctrl, buffer, len) < 0 ) {
        printf("failed to write\n");
        exit(EXIT_FAILURE);
    }

    free(buffer);

    /* make sure the test was started properly */
    if ( read_measured_response(ctrl, &response) < 0 ) {
        printf("failed to read response\n");
        exit(EXIT_FAILURE);
    }

    if ( response.code == MEASURED_CONTROL_OK ) {
        Log(LOG_DEBUG, "Test started OK on remote host");

        /* test started ok, wait for the result */
        if ( (bytes = read_control_packet(ctrl, &buffer)) < 0 ) {
            printf("failed to read\n");
            exit(EXIT_FAILURE);
        }

        in_msg = amplet2__measured__control__unpack(NULL, bytes, buffer);

        switch ( in_msg->type ) {
            /*
             * A result is what we are expecting - extract the actual result
             * data and send it to the printing function.
             */
            case AMPLET2__MEASURED__CONTROL__TYPE__RESULT: {
                amp_test_result_t result;

                if ( !in_msg->result || !in_msg->result->has_result ||
                        !in_msg->result->has_test_type ) {
                    break;
                }

                if ( test->id != in_msg->result->test_type ) {
                    printf("Unexpected test type, got %" PRIu64
                            " expected %" PRIu64 "\n",
                            in_msg->result->test_type, test->id);
                    break;
                }

                result.data = in_msg->result->result.data;
                result.len = in_msg->result->result.len;

                /* print using the test print functions, as if run locally */
                test->print_callback(&result);
                break;
            }

            /*
             * It's possible to receive an error if the test didn't run
             * correctly, just print the received error.
             */
            case AMPLET2__MEASURED__CONTROL__TYPE__RESPONSE: {
                Amplet2__Measured__Response runresponse;
                if ( parse_measured_response(buffer, bytes, &runresponse) < 0 ){
                    break;
                }
                printf("error running test: %d %s\n", runresponse.code,
                        runresponse.message);
                free(runresponse.message);
                break;
            }

            /* any other message type here is an error */
            default: {
                printf("unexpected message type %d\n", in_msg->type);
                break;
            }
        };

        free(buffer);
        amplet2__measured__control__free_unpacked(in_msg, NULL);
    } else {
        printf("error starting test: %d %s\n", response.code, response.message);
    }

    free(response.message);

    close_control_connection(ctrl);
    ssl_cleanup();

    freeaddrinfo(dest);
    unregister_tests();

    return EXIT_SUCCESS;
}
