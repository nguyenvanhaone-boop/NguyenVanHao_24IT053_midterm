#include "myls.h"
#include <stdlib.h>
#include <string.h>

static const Options *sort_options;

struct timespec entry_time(const Entry *e, const Options *opts)
{
#ifdef __APPLE__
    if (opts->time_field == 'a') return e->st.st_atimespec;
    if (opts->time_field == 'c') return e->st.st_ctimespec;
    return e->st.st_mtimespec;
#else
    if (opts->time_field == 'a') return e->st.st_atim;
    if (opts->time_field == 'c') return e->st.st_ctim;
    return e->st.st_mtim;
#endif
}

static int compare_entries(const void *left, const void *right)
{
    const Entry *a = left, *b = right;
    int result = 0;
    if (sort_options->size_sort) {
        result = (a->st.st_size < b->st.st_size) - (a->st.st_size > b->st.st_size);
    } else if (sort_options->time_sort) {
        struct timespec ta = entry_time(a, sort_options);
        struct timespec tb = entry_time(b, sort_options);
        result = (ta.tv_sec < tb.tv_sec) - (ta.tv_sec > tb.tv_sec);
        if (!result) result = (ta.tv_nsec < tb.tv_nsec) - (ta.tv_nsec > tb.tv_nsec);
    }
    if (!result) {
        int by_name = strcmp(a->name, b->name);
        result = (by_name > 0) - (by_name < 0);
    }
    return sort_options->reverse ? -result : result;
}

void sort_entries(EntryList *list, const Options *opts)
{
    if (!opts->no_sort && list->count > 1) {
        sort_options = opts;
        qsort(list->items, list->count, sizeof(Entry), compare_entries);
    }
}
