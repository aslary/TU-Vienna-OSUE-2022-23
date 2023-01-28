#include <limits.h>

#include "util.h"

/* ASCII color codes */
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_RESET "\x1b[0m"

/* A flag used to break a loop */
volatile sig_atomic_t quit = 0;

/* The program's name */
char *prog_name;

/**
 * @brief Prints the type of signum signal and sets the global quit value to 1
 *
 * @param signal A signum signal
 */
static void handle_signal(int signal) {
    print_signal(signal);
    quit = 1;
}

/**
 * @brief Typical usage function
 *
 * @param msg Message that will be printed if not empty
 */
static void usage(char *msg) {
    if (strlen(msg) != 0) {
        fprintf(stderr, "%s\n", msg);
    }
    fprintf(stderr, "Usage: %s\n", prog_name);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    prog_name = argv[0];

    if (argc != 1) {
        usage("Arguments must not be provided");
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0600);
    if (fd == -1) {
        print_errno_msg("shm_open failed");
    }

    if (ftruncate(fd, SHM_SIZE) < 0) {
        print_errno_msg("ftruncate failed");
    }

    cb_t *cb;
    cb = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (cb == MAP_FAILED) {
        print_errno_msg("mmap failed");
    }

    sem_t *free_sem = sem_open(FREE_SEM_NAME, O_CREAT | O_EXCL, 0600, NUMBER_OF_ENTRIES);
    sem_t *used_sem = sem_open(USED_SEM_NAME, O_CREAT | O_EXCL, 0600, 0);
    sem_t *write_sem = sem_open(WRITE_SEM_NAME, O_CREAT | O_EXCL, 0600, 1);
    if (used_sem == SEM_FAILED || free_sem == SEM_FAILED || write_sem == SEM_FAILED) {
        close_sem(free_sem, FREE_SEM_NAME);
        close_sem(used_sem, USED_SEM_NAME);
        close_sem(write_sem, WRITE_SEM_NAME);
        print_errno_msg("sem_open failed");
    }

    printf("Started supervisor with pid %d\n", getpid());
    cb_entry_t current_best;
    current_best.length = INT_MAX;
    while (!quit) {
        if (sem_wait(used_sem) == -1) {
            if (errno == EINTR) {
                continue;
            }
            close_sem(free_sem, FREE_SEM_NAME);
            close_sem(used_sem, USED_SEM_NAME);
            close_sem(write_sem, WRITE_SEM_NAME);
            print_errno_msg("sem_wait failed");
        }

        if (cb->entries[cb->rd % NUMBER_OF_ENTRIES].length == 0) {
            printf("%sThe graph is 3-colorable\n%s", ANSI_COLOR_GREEN, ANSI_COLOR_RESET);
            sem_post(free_sem);
            break;
        } else if (cb->entries[cb->rd].length >= current_best.length) {
            cb->rd = (cb->rd + 1) % NUMBER_OF_ENTRIES;
            sem_post(free_sem);
            continue;
        } else {
            current_best = cb->entries[cb->rd];
            printf(ANSI_COLOR_YELLOW);
            printf("Solution with %zu edge(s): ", cb->entries[cb->rd].length);
            print_cb_entry_t(cb->entries[cb->rd]);
            printf(ANSI_COLOR_RESET);

            cb->rd = (cb->rd + 1) % NUMBER_OF_ENTRIES;
            sem_post(free_sem);
        }
    }

    /* This will break the loop of the generator(s), which will cause their termination */
    cb->signal = 1;

    munmap(cb, SHM_SIZE);
    close(fd);
    shm_unlink(SHM_NAME);

    close_sem(free_sem, FREE_SEM_NAME);
    close_sem(used_sem, USED_SEM_NAME);
    close_sem(write_sem, WRITE_SEM_NAME);

    printf("Cleaned up all resources\n");
    return EXIT_SUCCESS;
}