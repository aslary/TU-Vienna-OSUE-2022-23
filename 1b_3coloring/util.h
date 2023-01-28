#include "ipc.h"

/**
 * @brief Prints a cb_entry_t with its details
 *
 * @param e The cb_entry_t to be printed
 */
void print_cb_entry_t(cb_entry_t e);

/**
 * @brief Prints the signum signal iff it is either SIGINT or SIGTERM
 *
 * @param signal The signum signal
 */
void print_signal(int signal);

/**
 * @brief Prints the content of errno with a msg. If msg is empty, then the format is different. Exits with EXIT_FAILURE
 *
 * @param msg The message to be printed alongside the content of errno
 */
void print_errno_msg(char *msg);

/**
 * @brief Closes and unlinks a named POSIX sem. Useful when clearing resources.
 *
 * @param sem The sem
 * @param name The name of the sem
 */
void close_sem(sem_t *sem, char *name);