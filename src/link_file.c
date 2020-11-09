/*
 * Transport links help multiplexing various input and output streams.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include "link.h"
#include "optparse.h"
#include "fatal.h"


typedef struct {
    link_t base;
    char file[256];
    FILE *f;
    int open_count;
    link_output_t output;
} link_file_t;


/* link_output functions */

static int out_write(link_output_t *output, const void *buf, size_t len)
{
    link_file_t *lf = (link_file_t *) output->link;

    return fwrite(buf, 1, len, lf->f);
}

static int out_vprintf(link_output_t *output, const char *fmt, va_list ap)
{
    link_file_t *lf = (link_file_t *) output->link;

    return vfprintf(lf->f, fmt, ap);
}

static FILE *out_get_stream(link_output_t *output)
{
    link_file_t *lf = (link_file_t *) output->link;

    return lf->f;
}

static void out_flush(link_output_t *output)
{
    link_file_t *lf = (link_file_t *) output->link;

    fprintf(lf->f, "\n");
    fflush(lf->f);
}

static void out_free(link_output_t *output)
{
    link_file_t *lf;

    if (!output)
        return;

    lf = (link_file_t *) output->link;
    if (lf->open_count <= 1) {
        fclose(lf->f);
        lf->f = NULL;
        lf->open_count = 0;
    } else
        lf->open_count--;
}

/* link functions */

static link_output_t *create_output(link_t *link, char *param, list_t *kwlist)
{
    link_file_t *lf = (link_file_t *) link;

    if (param && param[0] != '\0') {
        fprintf(stderr, "extra argument for link %s: %s\n", link->name, param);
        return NULL;
    }
    if (kwlist->len > 0) {
        fprintf(stderr, "extra parameters for link %s: %s\n", link->name, (const char *) kwlist->elems[0]);
        return NULL;
    }

    if (lf->open_count <= 0) {
        if (lf->file[0] == '\0') {
            lf->f = stdout;
            lf->open_count = 2; // never close
        } else {
            if (!(lf->f = fopen(lf->file, "a"))) {
                fprintf(stderr, "rtl_433: failed to open output file\n");
                return NULL;
            }
            lf->open_count = 1;
        }
    } else {
        lf->open_count++;
    }

    return &lf->output;
}

static void entry_free(link_t *link)
{
    if (!link)
        return;

    //!!!FIXME: what to do with the opened file?
    free(link);
}

link_t *link_file_create(list_t *links, const char *name, char *arg, list_t *kwargs)
{
    const link_file_t template = {.base = {.type = LINK_FILE, .create_output = create_output, .free = entry_free}, .output = {.write = out_write, .vprintf = out_vprintf, .get_stream = out_get_stream, .flush = out_flush, .free = out_free}};
    link_file_t *l;

    if (kwargs && kwargs->len != 0) {
        fprintf(stderr, "invalid link parameters\n");
        return NULL;
    }

    if (arg && strcmp(arg, "-") == 0)
        arg[0] = '\0';

    if (!name) {
        for (size_t i = 0; i < links->len; ++i) {
            l = links->elems[i];
            if (l->base.type == LINK_FILE && strcmp(l->file, arg ? arg : "") == 0) return &l->base;
        }
    }

    l = malloc(sizeof(link_file_t));
    if (!l) {
        WARN_MALLOC("link_file_create");
        return NULL;
    }

    *l = template;
    if (name)
        snprintf(l->base.name, sizeof(l->base.name), "%s", name);
    if (arg && arg[0] != '\0') {
        snprintf(l->file, sizeof(l->file), "%s", arg);
        arg[0] = '\0';
    }
    l->output.link = &l->base;

    list_push(links, l);

    return &l->base;
}
