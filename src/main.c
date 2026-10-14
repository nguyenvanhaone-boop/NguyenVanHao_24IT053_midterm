#include "myls.h"
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Ancestor {
    dev_t device;
    ino_t inode;
    const struct Ancestor *parent;
} Ancestor;

static bool output_started;

static void list_directory(const Entry *entry, const Options *opts,
                           bool header, const Ancestor *parent, unsigned depth)
{
    for (const Ancestor *p = parent; p; p = p->parent) {
        if (entry->st.st_dev == p->device && entry->st.st_ino == p->inode) {
            report_error(entry->path, ELOOP);
            return;
        }
    }
    /* Bound stack use for unusually deep trees, reporting incomplete traversal. */
    if (depth >= 512) {
        report_error(entry->path, EOVERFLOW);
        return;
    }
    Ancestor here = {entry->st.st_dev, entry->st.st_ino, parent};
    EntryList contents = {0};
    if (!collect_directory(entry->path, opts, &contents)) return;
    sort_entries(&contents, opts);
    if (header) {
        if (output_started) putchar('\n');
        print_text(entry->path, opts);
        puts(":");
    }
    print_entries(&contents, opts, true);
    output_started = true;
    if (opts->recursive) {
        for (size_t i = 0; i < contents.count; ++i) {
            Entry *child = &contents.items[i];
            if (S_ISDIR(child->st.st_mode) && strcmp(child->name, ".") &&
                strcmp(child->name, ".."))
                list_directory(child, opts, true, &here, depth + 1);
        }
    }
    entry_list_free(&contents);
}

int main(int argc, char **argv)
{
    setlocale(LC_CTYPE, "");
    Options opts;
    options_init(&opts);
    int first;
    if (options_parse(argc, argv, &opts, &first) < 0) return 2;
    EntryList files = {0}, directories = {0};
    int count = argc - first, operand_count = count ? count : 1;
    for (int i = 0; i < operand_count; ++i) {
        const char *path = count ? argv[first + i] : ".";
        struct stat st;
        if (!operand_stat(path, &opts, &st)) continue;
        EntryList *destination = S_ISDIR(st.st_mode) && !opts.directory_arg
                                 ? &directories : &files;
        entry_list_add(destination, path, path, &st);
    }
    sort_entries(&files, &opts);
    sort_entries(&directories, &opts);
    print_entries(&files, &opts, false);
    output_started = files.count != 0;
    for (size_t i = 0; i < directories.count; ++i)
        list_directory(&directories.items[i], &opts,
                       operand_count > 1 || opts.recursive, NULL, 0);
    entry_list_free(&files);
    entry_list_free(&directories);
    if (fflush(stdout) == EOF || ferror(stdout))
        report_error("standard output", errno ? errno : EIO);
    return exit_status;
}
