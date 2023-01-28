#include "util.h"

void print_cb_entry_t(cb_entry_t e) {
    for (int i = 0; i < e.length; i++) {
        printf("%d-%d ", e.from_vertices[i], e.to_vertices[i]);
    }
    printf("\n");
}

void print_signal(int signal) {
    if (signal == SIGINT) {
        printf("Handling SIGINT\n");
    }
    if (signal == SIGTERM) {
        printf("Handling SIGTERM\n");
    }
}

void print_errno_msg(char *msg) {
    if (strlen(msg) == 0) {
        fprintf(stderr, "%s\n", strerror(errno));
    } else {
        fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    }
    exit(EXIT_FAILURE);
}

void close_sem(sem_t *sem, char *name) {
    sem_close(sem);
    sem_unlink(name);
}