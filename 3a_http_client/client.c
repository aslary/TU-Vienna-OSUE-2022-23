#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// maximum buffer size
#define MAXLINE 4096

// special characters (except /)
#define SPECIAL_CHARS ";?:@=&"

// used to store arguments
typedef struct {
    int ps;                            // total occurences of the -p option
    int os;                            // total occurences of the -o option
    int ds;                            // total occurences of the -d option
    char p[MAXLINE];                   // value of the -p option
    char o[MAXLINE];                   // value of the -o option
    char d[MAXLINE];                   // value of the -d option, ending '/' symbol will be added if not present
    char url[MAXLINE * 2 + 1];         // the whole URL
    char host[253 + 1];                // extracted host part of the URL
    char resource[MAXLINE * 2 - 253];  // extracted resource part of the URL
} args_t;

// program arguments
args_t args = {.ps = 0, .os = 0, .ds = 0, .p = "80"};

// output file
FILE *outfile;

// program name
char *prog = "client";

/**
 * @brief Exits the program with EXIT_FAILURE
 *
 * @param msg Error message to be printed
 */
static void error_exit(char *msg) {
    if (errno == 0) {
        fprintf(stderr, "Error: %s\n", msg);
    } else {
        if (strlen(msg) == 0) {
            fprintf(stderr, "Error: %s\n", strerror(errno));
        } else {
            fprintf(stderr, "Error: %s: %s\n", msg, strerror(errno));
        }
    }
    exit(EXIT_FAILURE);
}

/**
 * @brief Typical usage method
 *
 * @param msg If length is at least one, additional info can be printed
 */
static void usage(char *msg) {
    if (strlen(msg) >= 1) {
        fprintf(stderr, "%s\n", msg);
    }
    fprintf(stderr, "Usage: %s [-p PORT] [ -o FILE | -d DIR ] URL\n", prog);
    exit(EXIT_FAILURE);
}

/**
 * @brief Exits the program with failure codes iff the response line (e.g. HTTP/1.1 XXX y...) is not version HTTP/1.1 and != 200 OK
 *
 * @param sockfile The connection to the server
 */
static void check_protocol_error(FILE *sockfile) {
    char buf[MAXLINE];

    if (fgets(buf, sizeof(buf), sockfile) == NULL) {
        error_exit("Could not receive status info");
    }

    char cpy[MAXLINE];
    strcpy(cpy, buf);

    char *version = strtok(buf, " ");
    char *status_code = strtok(NULL, " ");
    char *status_name = strtok(NULL, " ");

    if (version == NULL || status_code == NULL || status_name == NULL || strcmp(version, "HTTP/1.1") != 0) {
        fputs("Protocol error!\n", stderr);
        exit(2);
    }

    if (strcmp(status_code, "200") != 0) {
        fprintf(stderr, "%s\n", cpy + strlen("HTTP/1.1") + 1);
        exit(3);
    }

    // fputs(buf, outfile);
}

/**
 * @brief Outputs the response of the server in the outfile specified in args_t args.
 *
 * @param sockfile The connection to the server
 */
static void output(FILE *sockfile) {
    char buf[MAXLINE];

    // skip header
    while (fgets(buf, sizeof(buf), sockfile) != NULL) {
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
    }

    // output the content of the response into outfile
    while (fgets(buf, sizeof(buf), sockfile) != NULL) {
        fputs(buf, outfile);
    }
}

/**
 * @brief Parses the command line arguments. Checks for valid port, (in)valid options, etc.
 *
 * @param argc Number of arguments
 * @param argv The arguments
 */
static void parge_args(int argc, char *argv[]) {
    if (argc > 6) {
        usage("Too many arguments");
    }

    char opt;
    while ((opt = getopt(argc, argv, "p:o:d:")) != -1) {
        switch (opt) {
            case 'p':
                args.ps++;
                strncpy(args.p, optarg, sizeof(args.p));
                char *endptr;
                errno = 0;
                long val = strtol(args.p, &endptr, 10);
                if (errno != 0 || endptr == args.p || *endptr != '\0' || val < 0 || val > 65535) {
                    error_exit("Port must be element of interval [0..65535]");
                }
                break;
            case 'o':
                args.os++;
                strncpy(args.o, optarg, sizeof(args.o));
                break;
            case 'd':
                args.ds++;
                strncpy(args.d, optarg, sizeof(args.d));
                break;
            default:
                usage("Unkown option");
                break;
        }
    }

    if (argc - 1 != optind) {
        usage("No URL provided");
    }

    if (args.ps > 1 || args.os > 1 || args.ds > 1) {
        usage("Some options were provided too often");
    }

    if (args.os == 1 && args.ds == 1) {
        usage("Options -o and -d are mutually exclusive");
    }

    if (strstr(argv[optind], "http://") != argv[optind]) {
        usage("Only http:// URLs are supported");
    }

    strncpy(args.url, argv[optind], sizeof(args.url));

    if (strrchr(args.d, '/') != &args.d[strlen(args.d) - 1]) {
        strcat(args.d, "/");
    }
}

/**
 * @brief Sets the host and resource fields of the global args_t args.
 */
static void set_host_and_resource(void) {
    char *url_http_skipped = args.url + 7;

    char *resource = strchr(url_http_skipped, '/');
    if (resource == NULL) {
        strcpy(args.resource, "/");
        char *special = strpbrk(url_http_skipped, SPECIAL_CHARS);
        if (special != NULL) {
            strcat(args.resource, special);
        }
    } else {
        strcpy(args.resource, resource);
    }

    strncpy(args.host, url_http_skipped, strlen(url_http_skipped) - strlen(args.resource) + (resource == NULL ? 1 : 0));
}

/**
 * @brief Sets the outfile depending on which command line option was passed.
 * -o = outputfile, -d = either index.html or requested file, none of the both options = stdout
 */
static void set_outfile(void) {
    if (args.os == 1) {
        outfile = fopen(args.o, "w+");
    } else if (args.ds == 1) {
        char path[MAXLINE];
        strcpy(path, args.d);
        char *slash = strrchr(args.resource, '/');
        char *special = strpbrk(args.resource, SPECIAL_CHARS);
        if (special == NULL) {
            if (*(slash + 1) == '\0') {
                strcat(path, "index.html");
            } else {
                strcat(path, slash + 1);
            }
        } else {
            if (slash + 1 == special) {
                strcat(path, "index.html");
            } else {
                strncat(path, slash + 1, strlen(slash + 1) - strlen(special));
            }
        }
        // printf("Path: %s\n", path);
        outfile = fopen(path, "w+");
    } else {
        outfile = stdout;
    }

    if (outfile == NULL) {
        error_exit("fopen failed");
    }
}

/**
 * @brief Typical main function. Parses args and sets global args_t args struct.
 * Creates connection to server. Outputs response or fails program if any errors occured.
 *
 * @param argc The number of arguments
 * @param argv The arguments
 */
int main(int argc, char *argv[]) {
    prog = argv[0];
    parge_args(argc, argv);
    set_host_and_resource();
    set_outfile();

    // fprintf(stderr, "URL: %s\nHost: %s\nResource: %s\nPort: %s\nDir: %s\nOut: %s\n\n", args.url, args.host, args.resource, args.p, args.d, args.o);

    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int res = getaddrinfo(args.host, args.p, &hints, &ai);
    if (res != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
        exit(EXIT_FAILURE);
    }

    int sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sockfd == -1) error_exit("socket failed");
    if (connect(sockfd, ai->ai_addr, ai->ai_addrlen) == -1) error_exit("connect failed");

    FILE *sockfile = fdopen(sockfd, "r+");
    if (sockfile == NULL) error_exit("fdopen failed");

    fprintf(sockfile, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", args.resource, args.host);
    fflush(sockfile);

    check_protocol_error(sockfile);
    output(sockfile);

    // cleanup
    fclose(outfile);
    fclose(sockfile);
    freeaddrinfo(ai);

    return EXIT_SUCCESS;
}
