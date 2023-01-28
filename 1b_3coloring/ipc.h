#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define NUMBER_OF_ENTRIES 200
#define MAXIMUM_SOLUTION_LENGTH 8
#define SHM_NAME "/<your matriculation number>_shm"
#define SHM_SIZE (sizeof(cb_t))

#define FREE_SEM_NAME "/<your matriculation number>_free_sem"
#define USED_SEM_NAME "/<your matriculation number>_used_sem"
#define WRITE_SEM_NAME "/<your matriculation number>_write_sem"

/**
 * @brief Describes a vertex in a graph
 * @param key The label of a vertex
 * @param color The color of a vertex \in [0..2]
 */
typedef struct
{
    int key;
    int color;
} vertex_t;

/**
 * @brief Describes an edge in a graph
 * @details Addresses are used so that changes to the vertices are immediately available
 * @param v1 Holds the address of the left vertex
 * @param v2 Holds the address of the right vertex
 */
typedef struct
{
    vertex_t *v1;
    vertex_t *v2;
} edge_t;

/**
 * @brief Describes an entry to the circular buffer
 * @details Can semantically be viewed as a solution
 * @param length The amount of usable vertices
 * @param from_vertices Vertices to the left of the edge definition
 * @param to_vertices Vertices to the right of the edge definition
 */
typedef struct
{
    size_t length;
    int from_vertices[MAXIMUM_SOLUTION_LENGTH];
    int to_vertices[MAXIMUM_SOLUTION_LENGTH];
} cb_entry_t;

/**
 * @brief The circular buffer per se
 * @param signal So that the supervisor can inform the generator(s) to terminate
 * @param rd The current read position
 * @param wr The current write position
 * @param entries An array of fixed size that contains solutions provided by the generator(s)
 */
typedef struct
{
    int signal;
    int rd;
    int wr;
    cb_entry_t entries[NUMBER_OF_ENTRIES];
} cb_t;
