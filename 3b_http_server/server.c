#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// maximum buffer size
#define MAXLINE 4096

// HTTP responses
#define HTTP_STATUS_501_NOT_IMPLEMENTED "501 Not Implemented"
#define HTTP_STATUS_404_NOT_FOUND "404 Not Found"
#define HTTP_STATUS_400_BAD_REQUEST "400 Bad Request"
#define HTTP_STATUS_200_OK "200 OK"

// terminal colors
#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define YEL "\x1B[33m"
#define RESET "\x1B[0m"

// used to store arguments
typedef struct {
    int ps;
    int is;
    char port[5 + 1];
    char idx_file[MAXLINE];
    char doc_root[MAXLINE];
    char idx_path[MAXLINE * 2];
} args_t;

// used to store the current request
typedef struct {
    char *method;
    char *resource;
    char *version;
} request_t;

// program arguments
args_t args = {.ps = 0, .is = 0, .port = "8080", .idx_file = "index.html"};

// current request
request_t req;

// program name
char *prog = "server";

// connection to client
FILE *connection = NULL;

// requested resource
FILE *requested_resource = NULL;

// for signal handling
volatile sig_atomic_t quit = 0;

/**
 * @brief Sets quit flag to 1
 *
 * @param signal The signal
 */
static void handle_signal(int signal) { quit = 1; }

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
    fprintf(stderr, "Usage: %s [-p PORT] [-i INDEX] DOC_ROOT\n", prog);
    exit(EXIT_FAILURE);
}

/**
 * @brief Parses the command line arguments.
 *
 * @details Checks for port, (in)valid options, positional argument (DOC_ROOT) + if the DOC_ROOT dir exists.
 * Sets the global args_t args structure for convinient usage throughout the program.
 *
 * @param argc Number of arguments
 * @param argv The arguments
 */
static void parse_args(int argc, char *argv[]) {
    if (argc > 6) {
        usage("Too many arguments");
    }

    char opt;
    while ((opt = getopt(argc, argv, "p:i:")) != -1) {
        switch (opt) {
            case 'p':
                args.ps++;
                strncpy(args.port, optarg, sizeof(args.port));
                char *endptr;
                errno = 0;
                long val = strtol(args.port, &endptr, 10);
                if (errno != 0 || endptr == args.port || *endptr != '\0' || val < 0 || val > 65535) {
                    error_exit("Port must be element of interval [0..65535]");
                }
                break;
            case 'i':
                args.is++;
                strncpy(args.idx_file, optarg, sizeof(args.idx_file));
                break;
            default:
                usage("Unkown option");
                break;
        }
    }

    if (argc - 1 != optind) {
        usage("No DOC_ROOT provided");
    }

    if (args.is > 1 || args.ps > 1) {
        usage("Some options were provided too often");
    }

    strncpy(args.doc_root, argv[optind], sizeof(args.doc_root));
    if (strrchr(args.doc_root, '/') != &args.doc_root[strlen(args.doc_root) - 1]) {
        strcat(args.doc_root, "/");
    }
    snprintf(args.idx_path, sizeof(args.idx_path), "%s%s", args.doc_root, args.idx_file);

    DIR *dir = opendir(args.doc_root);
    if (dir == NULL) {
        error_exit("");
    }
    closedir(dir);
}

/**
 * @brief Stores the current date formatted as "%a, %d %b %y %X %Z" in the datestr array.
 *
 * @param datestr String array passed from the caller
 * @param size Size of datestr
 */
static void now(char datestr[], int size) {
    time_t t;
    struct tm *tmp;

    t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL) {
        error_exit("localtime");
    }

    if (strftime(datestr, size, "%a, %d %b %y %X %Z", tmp) == 0) {
        error_exit("strftime returned 0");
    }
}

/**
 * @brief Sends an HTTP response. Should only be used for error codes, even though 200 OK can be sent, it is bad practice to do so.
 *
 * @param http_status The status code with status text to be sent. Some predefined macros do exist.
 * @param req_line The HTTP request line, e.g. GET / HTTP/1.1\r\n
 */
static void fail(char *http_status, char *req_line) {
    fprintf(connection, "HTTP/1.1 %s\r\nConnection: close\r\n\r\n", http_status);
    printf(YEL);
    printf("\t[%s]: %s\n\n", http_status, req_line);
    printf(RESET);
    fclose(connection);
}

/**
 * @brief Checks if a request is valid. Only the request line is checked, the rest of the header is ignored.
 * If unvalid, a failure response is sent back to the client.
 *
 * @param req_line The HTTP request line, e.g. GET / HTTP/1.1\r\n
 *
 * @returns true, iff the request line is valid, false otherwise.
 */
static bool is_req_valid(char *req_line) {
    // copy so info logs have the full line
    char req_line_cpy[MAXLINE];
    strncpy(req_line_cpy, req_line, sizeof(req_line_cpy));

    req.method = strtok(req_line_cpy, " ");
    req.resource = strtok(NULL, " ");
    req.version = strtok(NULL, " ");

    if (req.method == NULL || req.resource == NULL || req.version == NULL || strcmp(req.version, "HTTP/1.1\r\n") != 0) {
        fail(HTTP_STATUS_400_BAD_REQUEST, req_line);
        return false;
    }

    if (strcmp(req.method, "GET") != 0) {
        fail(HTTP_STATUS_501_NOT_IMPLEMENTED, req_line);
        return false;
    }

    char path[MAXLINE * 2];
    if (strcmp(req.resource, "/") == 0) {
        strcpy(path, args.idx_path);
    } else {
        snprintf(path, sizeof(path), "%s%s", args.doc_root, strchr(req.resource, '/') == req.resource ? req.resource + 1 : req.resource);
    }
    requested_resource = fopen(path, "r");
    if (requested_resource == NULL) {
        fail(HTTP_STATUS_404_NOT_FOUND, req_line);
        return false;
    }

    return true;
}

/**
 * @brief Sends an HTTP 200 OK response.
 *
 * @param req_line The HTTP request line, e.g. GET / HTTP/1.1\r\n
 */
static void sendok(char *req_line) {
    char buf[MAXLINE];

    char date[200];
    now(date, sizeof(date));

    struct stat st;
    fstat(fileno(requested_resource), &st);
    int size = st.st_size;

    fprintf(connection, "HTTP/1.1 %s\r\nDate: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", HTTP_STATUS_200_OK, date, size);
    printf(GRN);
    printf("\t[200 OK]: %s\n\n", req_line);
    printf(RESET);

    while (fgets(buf, sizeof(buf), requested_resource) != NULL) {
        fputs(buf, connection);
    }

    fclose(connection);
}

/**
 * @brief If the request line validation was successful, this method skips the remaining HTTP header.
 */
static void skiphdr(void) {
    char buf[MAXLINE];
    while (fgets(buf, sizeof(buf), connection) != NULL) {
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
    }
}

/**
 * @brief Typical main method.
 *
 * @details Parses args, logs args, registers signal handler, sets up socket and listens for incoming connections.
 *
 * @param argc Number of arguments
 * @param argv The arguments
 */
int main(int argc, char *argv[]) {
    prog = argv[0];
    parse_args(argc, argv);
    printf("Port: %s\nIndex: %s\nDOC_ROOT: %s\nPath: %s\n\n", args.port, args.idx_file, args.doc_root, args.idx_path);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int res = getaddrinfo(NULL, args.port, &hints, &ai);
    if (res != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(res));
        exit(EXIT_FAILURE);
    }

    int sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sockfd < 0) {
        error_exit("socket failed");
    }

    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
        error_exit("bind failed");
    }

    freeaddrinfo(ai);

    if (listen(sockfd, 5) < 0) {
        error_exit("listen failed");
    }

    while (!quit) {
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);

        printf("Waiting for connection...\n");
        int connfd = accept(sockfd, NULL, NULL);
        if (connfd < 0) {
            if (errno != EINTR) {
                error_exit("accept failed");
            }
        }
        connection = fdopen(connfd, "r+");
        // here it is allowed to end the program with ctrl-c
        if (connection == NULL) {
            continue;
        }

        // after reaching this point, a client is connected, ctrl-c must not end the program
        printf("Connected...\n");
        sa.sa_flags = SA_RESTART;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);

        char *first_line = NULL;
        size_t n = 0;
        if (getline(&first_line, &n, connection) == -1) {
            free(first_line);
            printf(RED "Disconnected by client\n\n" RESET);
            continue;
        }

        skiphdr();
        if (!is_req_valid(first_line)) {
            free(first_line);
            continue;
        }
        sendok(first_line);

        free(first_line);
    }

    printf(RED "[TERMINATED]: Ctrl-C pressed\n" RESET);

    return EXIT_SUCCESS;
}
