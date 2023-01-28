#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NO_MSG ""

typedef struct {
    char *bin;      // program name
    long tabstops;  // value of the -t option
    u_int8_t ts;    // number of occurences of the -t option
    char *o;        // value of the -o option
    u_int8_t os;    // number of occurences of the -o option
    FILE *outfile;  // file to be written to
} args_t;

/* Global configuration */
args_t args = {.tabstops = 8};

/**
 * @brief Typical usage method
 */
static void usage(char *msg) {
    if (strcmp(msg, NO_MSG) != 0) {
        fprintf(stderr, "Error: %s\n", msg);
    }
    fprintf(stderr, "Usage: %s [-t tabstop] [-o outfile] [file...]\n", args.bin);
    exit(EXIT_FAILURE);
}

/**
 * @brief Typical exit method
 */
static void error_exit(char *msg) {
    if (strcmp(msg, NO_MSG) != 0) {
        fprintf(stderr, "%s\n", msg);
    }
    fprintf(stderr, "Error: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
}

/**
 * @brief Typical argument handling
 *
 * @param argc Number of arguments
 * @param argv The arguments
 */
static void handle_args(int argc, char **argv) {
    args.bin = argv[0];
    args.outfile = stdout;

    char cur;
    while ((cur = getopt(argc, argv, "t:o:")) != -1) {
        switch (cur) {
            case 't':
                args.ts++;
                if (args.ts > 1) {
                    usage("-t was provided too often");
                }

                char *end;
                args.tabstops = strtol(optarg, &end, 10);
                if (*end != '\0' || args.tabstops < 0) {
                    usage("Argument for option -t is not an integer >= 0");
                }
                break;
            case 'o':
                args.os++;
                if (args.os > 1) {
                    usage("-o was provided too often");
                }
                args.o = optarg;
                break;
            default:
                usage(NO_MSG);
        }
    }

    if (args.os == 1) {
        args.outfile = fopen(args.o, "a");
        if (args.outfile == NULL) {
            error_exit(NO_MSG);
        }
    }
}

/**
 * @brief Performs the expansion algorithm on the given input file
 *
 * @param in The input file to be read from
 */
static void expand(FILE *in) {
    if (in == NULL) {
        error_exit(NO_MSG);
    }

    int cur, x = 0, p = 0;
    while ((cur = fgetc(in)) != EOF) {
        if (cur == '\t') {
            p = args.tabstops * (x / args.tabstops + 1);
            while (x < p) {
                fputc(' ', args.outfile);
                x++;
            }
        } else {
            x = cur == '\n' ? 0 : x + 1;
            fputc(cur, args.outfile);
        }
    }
}

int main(int argc, char *argv[]) {
    handle_args(argc, argv);

    int len = argc - optind;
    if (len == 0) {
        expand(stdin);
    } else {
        for (int i = 0; i < len; i++) {
            FILE *in = fopen(argv[optind + i], "r");
            if (in == NULL) {
                fprintf(stderr, "Skipping '%s': %s\n", argv[optind + i], strerror(errno));
                continue;
            }
            expand(in);
            fclose(in);
        }
    }

    fclose(args.outfile);

    return EXIT_SUCCESS;
}
