/*
 * bfind - Breadth-first find
 *
 * A BFS version of the UNIX find utility using POSIX system calls.
 *
 * Usage: ./bfind [-L] [-xdev] [path...] [filters...]
 *
 * Filters:
 *   -name PATTERN   Glob match on filename (fnmatch)
 *   -type TYPE      f (file), d (directory), l (symlink)
 *   -mtime N        Modified within the last N days
 *   -size SPEC      File size: [+|-]N[c|k|M]
 *   -perm MODE      Exact octal permission match
 *
 * Options:
 *   -L              Follow symbolic links (default: no)
 *   -xdev           Do not cross filesystem boundaries
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "queue.h"


/* ------------------------------------------------------------------ */
/*  Filter definitions                                                 */
/* ------------------------------------------------------------------ */

typedef enum {
    FILTER_NAME,
    FILTER_TYPE,
    FILTER_MTIME,
    FILTER_SIZE,
    FILTER_PERM
} filter_kind_t;

typedef enum {
    SIZE_CMP_EXACT,
    SIZE_CMP_GREATER,
    SIZE_CMP_LESS
} size_cmp_t;

typedef struct {
    filter_kind_t kind;
    union {
        char *pattern;       /* -name */
        char type_char;      /* -type: 'f', 'd', or 'l' */
        int mtime_days;      /* -mtime */
        struct {
            off_t size_bytes;
            size_cmp_t size_cmp;
        } size;              /* -size */
        mode_t perm_mode;    /* -perm */
    } filter;
} filter_t;

/* ------------------------------------------------------------------ */
/*  Implement -xdev                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    char *path;
    dev_t root_dev;   /* device of the start path this item belongs to */
} qitem_t;

static qitem_t *qitem_new(const char *path, dev_t root_dev) {
    qitem_t *it = malloc(sizeof(*it));
    if (!it) return NULL;
    it->path = strdup(path);
    if (!it->path) {
        free(it);
        return NULL;
    }
    it->root_dev = root_dev;
    return it;
}

static void qitem_free(qitem_t *it) {
    if (!it) return;
    free(it->path);
    free(it);
}

/* ------------------------------------------------------------------ */
/*  Cycle detection                                                    */
/*                                                                     */
/*  A file's true on-disk identity is its (st_dev, st_ino) pair.       */
/*  You will need this for cycle detection when -L is set.             */
/* ------------------------------------------------------------------ */

typedef struct {
    dev_t dev;
    ino_t ino;
} dev_ino_t;

/* ------------------------------------------------------------------ */
/* Cycle detection helpers                                             */
/* ------------------------------------------------------------------ */

typedef struct visited_node {
    dev_t dev;
    ino_t ino;
    struct visited_node *next;
} visited_node_t;

static visited_node_t *g_visited_list = NULL;

/* returns true if we've already seen this directory */
static bool is_cycle(dev_t dev, ino_t ino) {
    visited_node_t *curr = g_visited_list;
    while (curr) {
        if (curr->dev == dev && curr->ino == ino) return true;
        curr = curr->next;
    }
    return false;
}

/* adds a directory to our "seen" list */
static void mark_visited(dev_t dev, ino_t ino) {
    visited_node_t *new_node = malloc(sizeof(visited_node_t));
    if (!new_node) {
        fprintf(stderr, "bfind: malloc failed\n");
        return;
    }
    new_node->dev = dev;
    new_node->ino = ino;
    new_node->next = g_visited_list;
    g_visited_list = new_node;
}

static void free_visited_list(void) {
    visited_node_t *p = g_visited_list;
    while (p) {
        visited_node_t *next = p->next;
        free(p);
        p = next;
    }
    g_visited_list = NULL;
}

/* ------------------------------------------------------------------ */
/*  Global configuration                                               */
/* ------------------------------------------------------------------ */

static filter_t *g_filters = NULL;
static int g_nfilters = 0;
static bool g_follow_links = false;
static bool g_xdev = false;
static dev_t g_start_dev = 0;
static time_t g_now;

/* ------------------------------------------------------------------ */
/*  Filter matching                                                    */
/* ------------------------------------------------------------------ */

/*
 * TODO 1: Implement this function.
 *
 * Return true if the single filter 'f' matches the file at 'path' with
 * metadata 'sb'. Handle each filter_kind_t in a switch statement.
 *
 * Refer to the assignment document for the specification of each filter.
 * Relevant man pages: fnmatch(3), stat(2).
 */
static bool filter_matches(const filter_t *f, const char *path,
                           const struct stat *sb) {
    // (void)f;
    // (void)path;
    // (void)sb;
    /* TODO: Your implementation here */
    switch (f->kind) {
        case FILTER_NAME: {
            /* Use strrchr to get just the filename, then fnmatch to compare */
            const char *filename = strrchr(path, '/');
            filename = (filename == NULL) ? path : filename + 1;
            return fnmatch(f->filter.pattern, filename, 0) == 0;
        }
        case FILTER_TYPE:
            if (f->filter.type_char == 'f') return S_ISREG(sb->st_mode);
            if (f->filter.type_char == 'd') return S_ISDIR(sb->st_mode);
            if (f->filter.type_char == 'l') return S_ISLNK(sb->st_mode);
            return false;
        case FILTER_PERM:
            /* Compare exact octal bits */
            return (sb->st_mode & 07777) == f->filter.perm_mode;
        case FILTER_SIZE: {
            off_t actual = sb->st_size;
            off_t target = f->filter.size.size_bytes;
            if (f->filter.size.size_cmp == SIZE_CMP_GREATER) return actual > target;
            if (f->filter.size.size_cmp == SIZE_CMP_LESS) return actual < target;
            return actual == target;
        }
        case FILTER_MTIME: {
            /* Calculate age in days: difftime returns seconds */
            double seconds = difftime(g_now, sb->st_mtime);
            int days = (int)(seconds / 86400);
            return days <= f->filter.mtime_days;
        }
    }
    return false;
}

/* Check if ALL filters match (AND semantics).
 * Returns true if every filter matches, false otherwise. */
static bool matches_all_filters(const char *path, const struct stat *sb) {
    for (int i = 0; i < g_nfilters; i++) {
        if (!filter_matches(&g_filters[i], path, sb)) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  Usage / help                                                       */
/* ------------------------------------------------------------------ */

static void print_usage(const char *progname) {
    printf("Usage: %s [-L] [-xdev] [path...] [filters...]\n"
           "\n"
           "Breadth-first search for files in a directory hierarchy.\n"
           "\n"
           "Options:\n"
           "  -L              Follow symbolic links\n"
           "  -xdev           Do not cross filesystem boundaries\n"
           "  --help          Display this help message and exit\n"
           "\n"
           "Filters (all filters are ANDed together):\n"
           "  -name PATTERN   Match filename against a glob pattern\n"
           "  -type TYPE      Match file type: f (file), d (dir), l (symlink)\n"
           "  -mtime N        Match files modified within the last N days\n"
           "  -size [+|-]N[c|k|M]\n"
           "                  Match file size (c=bytes, k=KiB, M=MiB)\n"
           "                  Prefix + means greater than, - means less than\n"
           "  -perm MODE      Match exact octal permission bits\n"
           "\n"
           "If no path is given, defaults to the current directory.\n",
           progname);
}

/* ------------------------------------------------------------------ */
/*  Argument parsing                                                   */
/* ------------------------------------------------------------------ */

/*
 * TODO 2: Implement this function.
 *
 * Parse a size specifier string into a byte count. The input is the
 * numeric portion (after any leading +/- is stripped by the caller)
 * with an optional unit suffix: 'c' (bytes), 'k' (KiB), 'M' (MiB).
 * No suffix means bytes.
 *
 * Examples: "100c" -> 100, "4k" -> 4096, "2M" -> 2097152, "512" -> 512
 */
static off_t parse_size(const char *arg) {
    /* TODO: Your implementation here */
    char *endptr;
    /* Use strtoll to handle large file sizes correctly */
    long long value = strtoll(arg, &endptr, 10);

    /* Check the suffix at the end of the numeric string */
    if (*endptr == 'c') {
        return (off_t)value;
    } else if (*endptr == 'k') {
        return (off_t)(value * 1024);
    } else if (*endptr == 'M') {
        return (off_t)(value * 1024 * 1024);
    }
    
    /* Default to bytes if no suffix is provided */
    return (off_t)value;
}

/*
 * TODO 3: Implement this function.
 *
 * Parse command-line arguments into options, paths, and filters.
 * See the usage string and assignment document for the expected format.
 *
 * Set the global variables g_follow_links, g_xdev, g_filters, and
 * g_nfilters as appropriate. Return a malloc'd array of path strings
 * and set *npaths. If no paths are given, default to ".".
 *
 * Handle --help by calling print_usage() and exiting.
 * Exit with an error for unknown options or missing filter arguments.
 */
static char **parse_args(int argc, char *argv[], int *npaths) {
    /* TODO: Your implementation here */
    int i = 1;
    /* 1. Parse Options */
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-L") == 0) {
            g_follow_links = true;
        } else if (strcmp(argv[i], "-xdev") == 0) {
            g_xdev = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            break; 
        }
        i++;
    }

    /* 2. Parse Paths */
    int path_start = i;
    int path_count = 0;
    while (i < argc && argv[i][0] != '-') {
        path_count++;
        i++;
    }

    char **paths;
    if (path_count == 0) {
        paths = malloc(sizeof(char *));
        paths[0] = strdup(".");
        *npaths = 1;
    } else {
        paths = malloc(sizeof(char *) * path_count);
        for (int j = 0; j < path_count; j++) {
            paths[j] = strdup(argv[path_start + j]);
        }
        *npaths = path_count;
    }

    /* 3. Parse Filters */
    g_filters = malloc(sizeof(filter_t) * (argc - i));
    while (i < argc) {
        filter_t *f = &g_filters[g_nfilters];
        if (strcmp(argv[i], "-name") == 0) {
            f->kind = FILTER_NAME;
            f->filter.pattern = argv[++i];
        } else if (strcmp(argv[i], "-type") == 0) {
            f->kind = FILTER_TYPE;
            f->filter.type_char = argv[++i][0];
        } else if (strcmp(argv[i], "-size") == 0) {
            f->kind = FILTER_SIZE;
            char *size_str = argv[++i];
            if (size_str[0] == '+') {
                f->filter.size.size_cmp = SIZE_CMP_GREATER;
                size_str++;
            } else if (size_str[0] == '-') {
                f->filter.size.size_cmp = SIZE_CMP_LESS;
                size_str++;
            } else {
                f->filter.size.size_cmp = SIZE_CMP_EXACT;
            }
            f->filter.size.size_bytes = parse_size(size_str);
        } else if (strcmp(argv[i], "-mtime") == 0) {
            f->kind = FILTER_MTIME;
            f->filter.mtime_days = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-perm") == 0) {
            f->kind = FILTER_PERM;
            f->filter.perm_mode = (mode_t)strtoul(argv[++i], NULL, 8);
        }
        g_nfilters++;
        i++;
    }
    return paths;
}

/* ------------------------------------------------------------------ */
/*  BFS traversal                                                      */
/* ------------------------------------------------------------------ */

/*
 * TODO 4: Implement this function.
 *
 * Traverse the filesystem breadth-first starting from the given paths.
 * For each entry, check the filters and print matching paths to stdout.
 *
 * You must handle:
 *   - The -L flag: controls whether symlinks are followed. Think about
 *     when to use stat(2) vs lstat(2) and what that means for descending
 *     into directories.
 *   - The -xdev flag: do not descend into directories on a different
 *     filesystem than the starting path (compare st_dev values).
 *   - Cycle detection (only relevant with -L): a symlink can point back
 *     to an ancestor directory. Only symlinks can create cycles (the OS
 *     forbids hard links to directories). Use the dev_ino_t type defined
 *     above to track visited directories — real directories should always
 *     be descended into, but symlinks to already-visited directories
 *     should be skipped.
 *   - Errors: if stat or opendir fails, print a message to stderr
 *     and continue traversing. Do not exit.
 *
 * The provided queue library (queue.h) implements a generic FIFO queue.
 */
static void bfs_traverse(char **start_paths, int npaths) {
    // (void)start_paths;
    // (void)npaths;
    /* TODO: Your implementation here */
    queue_t q;
    queue_init(&q);

    /* Use lstat by default, or stat if -L is set */
    int (*stat_func)(const char *, struct stat *) = g_follow_links ? stat : lstat;
    /* Initialize queue with starting paths */
    for (int i = 0; i < npaths; i++) {
        struct stat sb;
        
        if (stat_func(start_paths[i], &sb) != 0) {
            fprintf(stderr, "bfind: '%s': %s\n", start_paths[i], strerror(errno));
            continue;
        }

        qitem_t *it = qitem_new(start_paths[i], sb.st_dev);
        if (!it) {
            fprintf(stderr, "bfind: malloc failed\n");
            continue;
        }

        queue_enqueue(&q, it);
    }

    while (!queue_is_empty(&q)) {
        qitem_t *it = queue_dequeue(&q);
        char *curr_path = it->path;
        struct stat sb;

        if (stat_func(curr_path, &sb) != 0) {
            fprintf(stderr, "bfind: '%s': %s\n", curr_path, strerror(errno));
            free(curr_path);
            continue;
        }

        /* Print path if it matches all filters */
        if (matches_all_filters(curr_path, &sb)) {
            printf("%s\n", curr_path);
        }

        /* If it's a directory, prepare to explore its children */
        if (S_ISDIR(sb.st_mode)) {
            if (g_xdev && sb.st_dev != it->root_dev) {
                qitem_free(it);
                continue;
            }

            if (g_follow_links) {
                if (is_cycle(sb.st_dev, sb.st_ino)) {
                    free(curr_path);
                    continue;
                }
                mark_visited(sb.st_dev, sb.st_ino);
            }
            DIR *dir = opendir(curr_path);
            if (!dir) {
                fprintf(stderr, "bfind: cannot open '%s': %s\n", curr_path, strerror(errno));
            } else {
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    /* CRITICAL: Skip "." and ".." to avoid infinite loops */
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                        continue;
                    }

                    /* Build child path: curr_path + "/" + entry->d_name */
                    char child_path[PATH_MAX];
                    snprintf(child_path, sizeof(child_path), "%s/%s", curr_path, entry->d_name);
                    
                    qitem_t *child = qitem_new(child_path, it->root_dev);
                    queue_enqueue(&q, child);
                }
                closedir(dir);
            }
        }
        qitem_free(it);
    }
    queue_destroy(&q);
    free_visited_list();
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    g_now = time(NULL);

    int npaths;
    char **paths = parse_args(argc, argv, &npaths);

    bfs_traverse(paths, npaths);

    free(paths);
    free(g_filters);
    return 0;
}
