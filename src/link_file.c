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

#include "link.h"
#include "fatal.h"


typedef struct {
    link_t l;
    char file[256];
    FILE *f;
} link_file_t;


link_t *link_file_create(list_t *links, const char *name, const char *file)
{
    const link_file_t template = {.l = {.type = LINK_FILE}};
    link_file_t *l;
    static int count = 0;

    if (name) {
        for (size_t i = 0; i < links->len; ++i) {
            l = links->elems[i];
            if (strcasecmp(l->l.name, name) == 0) {
                return (l->l.type == LINK_FILE && strcmp(l->file, file) == 0) ? (link_t *) l : NULL;
            }
        }
    } else {
        for (size_t i = 0; i < links->len; ++i) {
            l = links->elems[i];
            if (l->l.type == LINK_FILE && strcmp(l->file, file) == 0) return (link_t *) l;
        }
    }

    if (!(l = malloc(sizeof(link_file_t)))) {
        WARN_MALLOC("link_file_create");
        return NULL;
    }

    *l = template;
    if (name)
        snprintf(l->l.name, sizeof(l->l.name), "%s", name);
    else
        snprintf(l->l.name, sizeof(l->l.name), "link%d", count++);
    snprintf(l->file, sizeof(l->file), "%s", file);

    list_push(links, l);

    return (link_t *) l;
}
