#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "http.h"
#include "servers.h"
#include "parsers.h"
#include "debug.h"

extern struct server_stats_t *server_list;
extern struct opt_t options;

/*
 * Don't do anything with the objects that we fetch to make up the page.
 */
size_t do_nothing(__attribute__((unused))void *ptr, size_t size,
        size_t nmemb, __attribute__((unused))void *data) {
    return size * nmemb;
}



/*
 * Record the cache control headers that were present in the response.
 * Currently checks for
 *
 *      Cache-Control:  public private no-cache no-store no-transform
 *                      must-revalidate proxy-revalidate max-age s-maxage
 *
 *      X-Cache: HIT MISS
 *      X-Cache-Lookup: HIT MISS
 */
size_t parse_headers(void *ptr, size_t size, size_t nmemb, void *data) {
    struct object_stats_t *object = (struct object_stats_t *)data;
    struct cache_headers_t *cache = &object->headers;
    char *buf = (char *)ptr;

    /* make sure the string is null terminated */
    /* XXX can this clobber useful data? or there is always a newline/null? */
    buf[size * nmemb] = '\0';

    /* check normal caching headers, they are usually all listed on one line */
    if ( strncmp(buf, "Cache-Control: ", strlen("Cache-Control: ")) == 0 ) {
        char *directives = buf + strlen("Cache-Control: ");
        char *end;
        int done = 0;

        while ( !done ) {
            /*
             * no more commas mean this is the last directive - process it
             * and then finish up
             */
            if ( (end = strstr(directives, ", ")) == NULL ) {
                done = 1;
            }

            /*
             * This is hideous but I can't think of a nicer way to do it.
             * strlen() should be used rather than a fixed value but the
             * added length turns it into an (even more) unreadable mess.
             */
            if ( strncmp(directives, "public", 6) == 0 ) {
                cache->flags.pub = 1;
            } else if ( strncmp(directives, "private", 7) == 0 ) {
                cache->flags.priv = 1;
            } else if ( strncmp(directives, "no-cache", 8) == 0 ) {
                cache->flags.no_cache = 1;
            } else if ( strncmp(directives, "no-store", 8) == 0 ) {
                cache->flags.no_store = 1;
            } else if ( strncmp(directives, "no-transform", 12) == 0 ) {
                cache->flags.no_transform = 1;
            } else if ( strncmp(directives, "must-revalidate", 15) == 0 ) {
                cache->flags.must_revalidate = 1;
            } else if ( strncmp(directives, "proxy-revalidate", 16) == 0 ) {
                cache->flags.proxy_revalidate = 1;
            } else if ( strncmp(directives, "max-age", 7) == 0 ) {
                sscanf(directives, "max-age= %d", &cache->max_age);
            } else if ( strncmp(directives, "s-maxage", 8) == 0 ) {
                sscanf(directives, "s-maxage= %d", &cache->s_maxage);
            } else {
                Log(LOG_DEBUG, "skipping unknown directive: '%s'\n",directives);
            }

            /* skip the comma and the space to get the next directive */
            directives = end + 2;
        }

    } else if ( strncmp(buf, "X-Cache: ", strlen("X-Cache: ")) == 0 ) {
        char *directives = buf + strlen("X-Cache: ");

        if ( strncmp(directives, "HIT", strlen("HIT")) == 0 ) {
            cache->x_cache = 1;
        } else if ( strncmp(directives, "MISS", strlen("MISS")) == 0 ) {
            cache->x_cache = 0;
        } else {
            Log(LOG_DEBUG, "skipping unknown x-cache response: '%s'\n",
                    directives);
        }

    } else if(strncmp(buf,"X-Cache-Lookup: ",strlen("X-Cache-Lookup: ")) == 0) {
        char *directives = buf + strlen("X-Cache-Lookup: ");

        if ( strncmp(directives, "HIT", strlen("HIT")) == 0 ) {
            cache->x_cache_lookup = 1;
        } else if ( strncmp(directives, "MISS", strlen("MISS")) == 0 ) {
            cache->x_cache_lookup = 0;
        } else {
            Log(LOG_DEBUG, "skipping unknown x-cache response: '%s'\n",
                    directives);
        }

    } else if (strncmp(buf,"HTTP/1.0 200 OK",strlen("HTTP/1.0 200 OK")) == 0) {
        /* we might get a "Connection: keep-alive" header here, but we won't
         * be able to do pipelining over a HTTP/1.0 connection, so limit it
         * to 1 outstanding request
         */
        struct server_stats_t *server;
        get_server(object->server_name, server_list, &server);
        if ( options.pipelining ) {//XXX global now...
            server->pipelining_maxrequests = 1;
        }

    } else if (strncmp(buf,"HTTP/1.1 200 OK",strlen("HTTP/1.1 200 OK")) == 0) {
        /* every HTTP/1.1 connection should be able to do pipelining, lets
         * take that gamble
         */
        struct server_stats_t *server;
        get_server(object->server_name, server_list, &server);
        if ( options.pipelining ) {//XXX global now...
            server->pipelining_maxrequests = options.pipelining_maxrequests;
        }

    } else {
        Log(LOG_DEBUG, "ignored header: %s\n", buf);
    }

    return size * nmemb;
}


/*
 * Walk through the buffer looking for any external resources that we should
 * also download to complete the page. Anything pointed to by "src=" inside
 * of <script> and <img> tags, or "href=" inside of <link> will be fetched.
 *
 * Check lexer.l to see how they are extracted from the page source.
 */
size_t parse_response(void *ptr, size_t size, size_t nmemb, void *data) {
    int pipefd[2];
    FILE *writer;
    int written = 0;
    int total = size * nmemb;
    extern FILE *yyin;
    int yylex(void);

    if ( pipe(pipefd) == -1 ) {
        Log(LOG_ERR, "error creating pipe\n");
        exit(1);
    }

    /* open a pipe for writing data to the lexer */
    fcntl(pipefd[1], O_NONBLOCK, 1);
    if ( (writer = fdopen(pipefd[1], "w")) == NULL ) {
        Log(LOG_ERR, "error opening pipe for writing: %d\n", errno);
        exit(1);
    }

    /* open pipe for reading data into the lexer */
    if ( (yyin = fdopen(pipefd[0], "r")) == NULL ) {
        Log(LOG_ERR, "error opening pipe for reading: %d\n", errno);
        exit(1);
    }

    /*
     * Send everything curl gave us on to the lexer, which will create objects
     * and queue them as it finds them
     */
    do {
        written += fwrite(ptr, size, nmemb, (FILE*)writer);

        if ( written < total ) {
            //fprintf(stderr, "incomplete write\n");
            if ( ferror(writer) && errno != EAGAIN ) {
                /* breaking here will return < total, which will error */
                fclose(writer);
                writer = NULL;
                break;
            } else {
                //fprintf(stderr, "EAGAIN\n");
            }
        }

        /* finished, make sure all the data has got through */
        if ( written == total) {
            fflush(writer);
            fclose(writer);
            writer = NULL;
        }

        yylex();
    } while ( !ferror(data) && written < total );

    fclose(yyin);

    return written;
}