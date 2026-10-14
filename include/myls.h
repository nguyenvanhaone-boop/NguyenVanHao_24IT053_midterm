#ifndef MYLS_H
#define MYLS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
    bool all, almost_all, classify, no_sort, human, inode, kilobytes;
    int long_format; /* 0: names only, 1: long, 2: numeric IDs */
    bool recursive, reverse, size_sort, blocks, time_sort;
    char time_field; /* m: modification, a: access, c: status change */
    int print_mode;  /* 0: terminal-dependent, 1: question marks, 2: raw */
    bool directory_arg;
    uintmax_t block_size;
} Options;

typedef struct {
    char *name;
    char *path;
    struct stat st;
} Entry;

typedef struct {
    Entry *items;
    size_t count, capacity;
} EntryList;

extern int exit_status;
void report_error(const char *path, int error);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *join_path(const char *parent, const char *name);
char *link_target(const char *path);
void options_init(Options *opts);
int options_parse(int argc, char **argv, Options *opts, int *first);
void entry_list_free(EntryList *list);
void entry_list_add(EntryList *list, const char *name, const char *path,
                    const struct stat *st);
bool collect_directory(const char *path, const Options *opts, EntryList *list);
bool operand_stat(const char *path, const Options *opts, struct stat *st);
struct timespec entry_time(const Entry *entry, const Options *opts);
void sort_entries(EntryList *list, const Options *opts);
void print_text(const char *text, const Options *opts);
void print_entries(const EntryList *list, const Options *opts, bool directory);
#endif
