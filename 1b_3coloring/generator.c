#include <ctype.h>

#include "util.h"

/**
 * @brief Parses the command line arguments into graph datastructures for later use
 *
 * @param argc The number of CLI args
 * @param argv The CLI argument values per se
 * @param edges An edges array passed from outside in which the edges are going to be stored
 * @param edges_length A size_t from outside in which the number of usable edges is stored
 * @param vertices A vertex array passed from outside in which the vertices are going to be stored uniquely by their key
 * @param vertices_length A size_t from outside in which the number of unique vertices is stored
 */
static void parse_argv(int argc, char *argv[],
                       edge_t edges[], size_t *edges_length,
                       vertex_t vertices[], size_t *vertices_length);

/**
 * @brief Adds a new edge (consisting of v1,v2) into the edges array at the position of edges_length
 *
 * @param edges An edges array passed from outside
 * @param edges_length The number of usable edges
 * @param v1 The left vertex of the new edge
 * @param v2 The right vertex of the new edge
 */
static void add_new_edge(edge_t edges[], size_t *edges_length, vertex_t *v1, vertex_t *v2);

/**
 * @brief Adds a new vertex into the vertices array at the position of vertices_length
 *
 * @param vertices A vertex array passed from outside
 * @param vertices_length The number of usable vertices
 * @param key The key that should be used to label the new vertex
 * @return vertex_t* The address of the newly added vertex
 */
static vertex_t *add_new_vertex(vertex_t vertices[], size_t *vertices_length, long key);

/**
 * @brief Tries to find edges that already exist by their vertices
 * @details Remember that edges are equal regardless of the order of their vertices
 *
 * @param edges The edges array to be searched
 * @param edges_length The number of usable edges
 * @param v1 A vertex (either left or right)
 * @param v2 A vertex (either left or right)
 * @return edge_t* The address of the found edge or NULL if it was not found
 */
static edge_t *find_edge_by_vertices(edge_t edges[], size_t edges_length, vertex_t *v1, vertex_t *v2);

/**
 * @brief Tries to find vertices that vertices that already exist by their key
 *
 * @param vertices The vertices array to be searched
 * @param vertices_length The number of usable vertices
 * @param key The key to be compared
 * @return vertex_t* The address of the found vertex or NULL if it was not found
 */
static vertex_t *find_vertex_by_key(vertex_t vertices[], size_t vertices_length, long key);

/**
 * @brief Changes the color of each usable vertex inside the vertices array
 *
 * @param vertices The array of vertices to be randomized
 * @param vertices_length The number of usable vertices
 */
static void randomize(vertex_t vertices[], size_t vertices_length);

/**
 * @brief Returns the number of edges that connect vertices with the same color and saves those edges inside the removal_candidates array
 *
 * @param edges The edge array to be searched
 * @param edges_length The number of usable edges
 * @param removal_candidates An array from outside that will be changed inside the function
 * @return size_t The number of edges that connect vertices with the same color (essentially, this can be viewed as removal_candidates_length)
 */
static size_t set_removal_candidates(edge_t edges[], size_t edges_length, edge_t removal_candidates[]);

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
    fprintf(stderr, "SYNOPSIS\n\t%s edge [edge...]\nEXAMPLE\n\t%s 0-1 0-2 0-3 1-2 1-3 2-3\n", prog_name, prog_name);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    prog_name = argv[0];

    if (argc < 2) {
        usage("At least one edge must be provided");
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* The process id is a good seed value */
    srand(getpid());

    size_t edges_length = 0;
    edge_t edges[argc - 1];
    size_t vertices_length = 0;
    vertex_t vertices[(argc - 1) * 2];

    parse_argv(argc, argv, edges, &edges_length, vertices, &vertices_length);

    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd == -1) {
        print_errno_msg("shm_open failed");
    }

    cb_t *cb;
    cb = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (cb == MAP_FAILED) {
        print_errno_msg("mmap failed");
    }

    sem_t *free_sem = sem_open(FREE_SEM_NAME, 0);
    sem_t *used_sem = sem_open(USED_SEM_NAME, 0);
    sem_t *write_sem = sem_open(WRITE_SEM_NAME, 0);

    if (used_sem == SEM_FAILED || free_sem == SEM_FAILED || write_sem == SEM_FAILED) {
        close_sem(free_sem, FREE_SEM_NAME);
        close_sem(used_sem, USED_SEM_NAME);
        close_sem(write_sem, WRITE_SEM_NAME);
        print_errno_msg("sem_open failed");
    }

    printf("Started generator with pid %d\n", getpid());
    while (!quit && cb->signal == 0) {
        if (sem_wait(write_sem) == -1) {
            if (errno == EINTR) {
                continue;
            }
            close_sem(free_sem, FREE_SEM_NAME);
            close_sem(used_sem, USED_SEM_NAME);
            close_sem(write_sem, WRITE_SEM_NAME);
            print_errno_msg("sem_wait failed");
        }

        if (sem_wait(free_sem) == -1) {
            if (errno == EINTR) {
                continue;
            }
            close_sem(free_sem, FREE_SEM_NAME);
            close_sem(used_sem, USED_SEM_NAME);
            close_sem(write_sem, WRITE_SEM_NAME);
            print_errno_msg("sem_wait failed");
        }

        randomize(vertices, vertices_length);
        edge_t removal_candidates[MAXIMUM_SOLUTION_LENGTH];
        size_t removal_candidates_length = set_removal_candidates(edges, edges_length, removal_candidates);
        if (removal_candidates_length > MAXIMUM_SOLUTION_LENGTH) {
            continue;
        }

        cb->entries[cb->wr].length = removal_candidates_length;
        for (int i = 0; i < removal_candidates_length; i++) {
            cb->entries[cb->wr].from_vertices[i] = removal_candidates[i].v1->key;
            cb->entries[cb->wr].to_vertices[i] = removal_candidates[i].v2->key;
        }
        cb->wr = (cb->wr + 1) % NUMBER_OF_ENTRIES;

        printf("Reported solution with %zu edge(s)\n", removal_candidates_length);
        sem_post(used_sem);
        sem_post(write_sem);
    }

    if (cb->signal == 1) {
        printf("Terminated by order of the supervisor process\n");
    }

    munmap(cb, SHM_SIZE);
    close(fd);

    close_sem(free_sem, FREE_SEM_NAME);
    close_sem(used_sem, USED_SEM_NAME);
    close_sem(write_sem, WRITE_SEM_NAME);

    printf("Cleaned up all resources\n");
    return EXIT_SUCCESS;
}

static void parse_argv(int argc, char *argv[], edge_t edges[], size_t *edges_length, vertex_t vertices[], size_t *vertices_length) {
    for (int i = 1; i < argc; i++) {
        size_t tokens = 1;
        char *token = strtok(argv[i], "-");
        char *v1_key = token;
        char *v2_key;

        while ((token = strtok(NULL, "-")) != NULL) {
            v2_key = token != NULL ? token : v2_key;
            tokens++;
        }

        if (tokens != 2) {
            usage("edges must consist of exactly two vertices");
        }

        char *v1_next, *v2_next;
        int v1_key_parsed = (int)strtol(v1_key, &v1_next, 10);
        int v2_key_parsed = (int)strtol(v2_key, &v2_next, 10);

        if (v1_next == v1_key || *v1_next != '\0' || v2_next == v2_key || *v2_next != '\0') {
            usage("vertex keys must consist of digits only");
        }

        vertex_t *v1 = find_vertex_by_key(vertices, *vertices_length, v1_key_parsed);
        if (v1 == NULL) {
            v1 = add_new_vertex(vertices, vertices_length, v1_key_parsed);
        }

        vertex_t *v2 = find_vertex_by_key(vertices, *vertices_length, v2_key_parsed);
        if (v2 == NULL) {
            v2 = add_new_vertex(vertices, vertices_length, v2_key_parsed);
        }

        edge_t *e = find_edge_by_vertices(edges, *edges_length, v1, v2);
        if (e == NULL) {
            add_new_edge(edges, edges_length, v1, v2);
        }
    }
}

static void add_new_edge(edge_t edges[], size_t *edges_length, vertex_t *v1, vertex_t *v2) {
    edges[*edges_length].v1 = v1;
    edges[*edges_length].v2 = v2;
    (*edges_length)++;
}

static vertex_t *add_new_vertex(vertex_t vertices[], size_t *vertices_length, long key) {
    vertices[*vertices_length].key = key;
    vertices[*vertices_length].color = 0;
    (*vertices_length)++;

    return &vertices[*vertices_length - 1];
}

static edge_t *find_edge_by_vertices(edge_t edges[], size_t edges_length, vertex_t *v1, vertex_t *v2) {
    for (int i = 0; i < edges_length; i++) {
        if ((edges[i].v1 == v1 && edges[i].v2 == v2) || (edges[i].v1 == v2 && edges[i].v2 == v1)) {
            return &edges[i];
        }
    }
    return NULL;
}

static vertex_t *find_vertex_by_key(vertex_t vertices[], size_t vertices_length, long key) {
    for (int i = 0; i < vertices_length; i++) {
        if (vertices[i].key == key) {
            return &vertices[i];
        }
    }
    return NULL;
}

static void randomize(vertex_t vertices[], size_t vertices_length) {
    for (int i = 0; i < vertices_length; i++) {
        vertices[i].color = rand() % 3;
    }
}

static size_t set_removal_candidates(edge_t edges[], size_t edges_length, edge_t removal_candidates[]) {
    size_t j = 0;
    for (int i = 0; i < edges_length && j < MAXIMUM_SOLUTION_LENGTH; i++) {
        if (edges[i].v1->color == edges[i].v2->color) {
            removal_candidates[j] = edges[i];
            j++;
        }
    }
    return j;
}
