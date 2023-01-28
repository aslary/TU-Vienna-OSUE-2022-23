/**
 * @file forkFFT.c
 * @brief Implementation of the fast fourier transformation using child processes.
 * @details A program that recursively creates child processes in a binary-tree-like manner.
 * This approach is different than shm from the last homework. In order to communicate, unnamed pipes are used.
 * There are two pipes: for even and odd ones. In each recursion step, even and odd values are read.
 * Values are reported to the parent with the corresponding pipe.
 * Also, the complex number API from C is used here, since it helps with addition and multiplication.
 **/

#include <complex.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define INVALID_CHARACTERS "Invalid character(s) in input"  // error message that occurs more than once
#define HIGH_PRECISION 6                                    // 6 decimals (for printf)
#define LOW_PRECISION 3                                     // 3 decimals (for printf)
#define MAX_BUFFER 255                                      // maximum line length

// variable to store the option -p
int opt_p = -1;

// program name
char *pname = "forkFFT";

/**
 * @brief Exits the program with EXIT_FAILURE
 *
 * @param msg Error message to be printed
 */
static void error_exit(char *msg) {
    // errno is set to 0 in str_to_complex for input validation
    // to prevent "Success" from being printed, following if is needed
    if (errno == 0) {
        fprintf(stderr, "Error: %s\n", msg);
    } else {
        fprintf(stderr, "Error: %s: %s\n", msg, strerror(errno));
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
    fprintf(stderr, "Usage: %s [-p]\n", pname);
    exit(EXIT_FAILURE);
}

/**
 * @brief Creates the pipes, maps them if it is currently in the child process and recursively calls itself (2^n programs),
 * otherwise: specifies were the parent reads and writes (stores for the caller)
 * @details remember: write[index 1] -> ([[pipe]]) -> read[index 0]
 *
 * @param fd_read fd of read end
 * @param fd_write fd of write end
 * @param pid
 */
static void pipe_and_fork(int *fd_read, int *fd_write, pid_t *pid) {
    int pipes[2][2];

    if (pipe(pipes[0]) == -1 || pipe(pipes[1]) == -1) {
        error_exit("pipe failed");
    }

    *pid = fork();

    switch (*pid) {
        case -1:
            error_exit("fork failed");
            break;
        case 0:
            close(pipes[0][1]);
            close(pipes[1][0]);
            if (dup2(pipes[0][0], STDIN_FILENO) == -1 || dup2(pipes[1][1], STDOUT_FILENO) == -1) {
                error_exit("dup2 failed");
            }
            close(pipes[0][0]);
            close(pipes[1][1]);
            if (execlp(pname, pname, NULL) == -1) {
                error_exit("execlp failed");
            }
        default:
            close(pipes[0][0]);
            close(pipes[1][1]);
            *fd_write = pipes[0][1];
            *fd_read = pipes[1][0];
            break;
    }
}

/**
 * @brief Prints a complex number of the C API to stdout, while also calculating an epsilon for correct sign
 *
 * @param z Complex number to be printed
 */
static void print_complex(float complex z) {
    float real = creal(z);
    float imag = cimag(z);
    if (opt_p == LOW_PRECISION) {
        float epsilon = -1.0 / pow(10, LOW_PRECISION);
        // using abs removes decimals, use fabs
        if (real > epsilon) {
            real = fabs(real);
        }
        if (imag > epsilon) {
            imag = fabs(imag);
        }
    }
    printf("%.*f %.*f*i\n", opt_p, real, opt_p, imag);
}

/**
 * @brief Useful to print errors that can occur when using strtof
 *
 * @param x The float
 * @param str The initial string
 * @param endptr The position inside "str" after strtof was called
 */
static void print_conversion_error(float x, char *str, char *endptr) {
    if (errno == ERANGE || (errno != 0 && x == 0)) {
        error_exit("HUGE_VAL, HUGE_VALF, or HUGE_VALL occured");
    }
    if (endptr == str) {
        error_exit(INVALID_CHARACTERS);
    }
}

/**
 * @brief Returns a string as a complex number. If has_more is true, then strings as defined in print_complex may also be processed,
 * otherwise only floating point numbers will be turned into a complex number without imaginary part
 *
 * @param str The string to be converted
 * @param has_more The flag specified in @brief
 * @return float str turned into a complex number
 */
static float complex str_to_complex(char *str, int has_more) {
    errno = 0;
    char *endptr;
    float real = strtof(str, &endptr);
    float imag;
    float complex z;

    print_conversion_error(real, str, endptr);

    switch (*endptr) {
        case ' ':
            if (!has_more) {
                error_exit(INVALID_CHARACTERS);
            }
            imag = strtof(endptr, &endptr);
            print_conversion_error(imag, str, endptr);
            z = real + I * imag;
            break;
        case '\n':
            z = real;
            break;
        default:
            error_exit(INVALID_CHARACTERS);
    }

    return z;
}

/**
 * @brief The actual FFT transformation. Reads values from the even pipe and odd pipe.
 *
 * @param fd_even_read fd of the even pipe's read end
 * @param fd_odd_read fd of the odd pipe's read end
 * @param n number of numbers
 */
static void fft(int fd_even_read, int fd_odd_read, int n) {
    char buffer_1[MAX_BUFFER];
    char buffer_2[MAX_BUFFER];

    float complex R_even;
    float complex R_odd;
    float complex R[n];

    FILE *pipe_even_read = fdopen(fd_even_read, "r");
    FILE *pipe_odd_read = fdopen(fd_odd_read, "r");

    for (int k = 0; k < n / 2; k++) {
        if ((fgets(buffer_1, MAX_BUFFER, pipe_even_read) == NULL) | (fgets(buffer_2, MAX_BUFFER, pipe_odd_read) == NULL)) {
            fclose(pipe_even_read);
            fclose(pipe_odd_read);
            error_exit("read failed");
        }

        // formula specified in task sheet
        float complex help = cos((-2 * M_PI * k) / n) + I * sin((-2 * M_PI * k) / n);
        R_even = str_to_complex(buffer_1, 1);
        R_odd = str_to_complex(buffer_2, 1);
        R[k] = R_even + help * R_odd;
        R[k + n / 2] = R_even - help * R_odd;
    }

    fclose(pipe_even_read);
    fclose(pipe_odd_read);

    for (int i = 0; i < n; i++) {
        print_complex(R[i]);
    }
}

/**
 * @brief Handles argc and argv from main(). Allows -p or -P option and sets the precision for @see{print_complex}.
 *
 * @param argc argc
 * @param argv argv
 */
static void handle_args(int argc, char **argv) {
    pname = argv[0];
    switch (argc) {
        case 1:
            opt_p = HIGH_PRECISION;
            break;
        case 2:
            if (strcasecmp(argv[1], "-p") != 0) {
                usage("Unknown option");
            }
            opt_p = LOW_PRECISION;
            break;
        default:
            usage("Too many arguments");
            break;
    }
}

/**
 * @brief Used to prevent code duplication. Waits for the child specified with pid and closes the fds on error.
 *
 * @param fd_even_read fd of the even pipe's read end
 * @param fd_odd_read fd of the odd pipe's read end
 * @param pid
 */
static void my_waitpid(int fd_even_read, int fd_odd_read, pid_t pid) {
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        error_exit("wait failed");
    }
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) == EXIT_FAILURE) {
            close(fd_even_read);
            close(fd_odd_read);
            error_exit("child failed");
        }
    }
}

/**
 * @brief Typical main method
 *
 * @param argc argc
 * @param argv argv
 * @return int return code
 */
int main(int argc, char **argv) {
    handle_args(argc, argv);
    size_t n_lines = -1;

    // even
    int fd_even_read, fd_even_write;
    pid_t pid_even;
    char buffer_even[MAX_BUFFER];

    // odd
    int fd_odd_read, fd_odd_write;
    pid_t pid_odd;
    char buffer_odd[MAX_BUFFER];

    // try reading a value
    if (fgets(buffer_even, MAX_BUFFER, stdin) == NULL) {
        error_exit("read failed");
    }

    // only one value was provided, so prevent endless recursion with following stop condition
    if (fgets(buffer_odd, MAX_BUFFER, stdin) == NULL) {
        print_complex(str_to_complex(buffer_even, 0));
        exit(EXIT_SUCCESS);
    }

    // two lines were read
    n_lines = 2;

    // map pipes and create children (2^n)
    pipe_and_fork(&fd_even_read, &fd_even_write, &pid_even);
    pipe_and_fork(&fd_odd_read, &fd_odd_write, &pid_odd);

    // open write ends of pipes
    FILE *pipe_even_write = fdopen(fd_even_write, "w");
    FILE *pipe_odd_write = fdopen(fd_odd_write, "w");

    // writing the user input in the write end of each pipe
    if (fputs(buffer_even, pipe_even_write) == EOF || fputs(buffer_odd, pipe_odd_write) == EOF) {
        error_exit("Write failed");
    }

    // keep reading pairs of lines and output them to the corresponding pipes for consumption
    while (fgets(buffer_even, MAX_BUFFER, stdin) != NULL) {
        if (fputs(buffer_even, pipe_even_write) == EOF) {
            error_exit("Write failed");
        }
        // this is important, because 2^n processes are created, shutting down the input while they want to read will result in errors and possible leaks
        if (fgets(buffer_odd, MAX_BUFFER, stdin) == NULL) {
            close(fd_even_read);
            fclose(pipe_even_write);
            close(fd_odd_read);
            fclose(pipe_odd_write);
            error_exit("Number of lines has to be a power of 2");
        }
        if (fputs(buffer_odd, pipe_odd_write) == EOF) {
            error_exit("Write failed");
        }

        // two lines were read
        n_lines += 2;
    }

    fclose(pipe_even_write);
    fclose(pipe_odd_write);
    my_waitpid(fd_even_read, fd_odd_read, pid_even);
    my_waitpid(fd_even_read, fd_odd_read, pid_odd);
    fft(fd_even_read, fd_odd_read, n_lines);

    exit(EXIT_SUCCESS);
}
