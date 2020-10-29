/** @file
 *  Transport links help multiplexing various input and output streams.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INCLUDE_LINK_H_
#define INCLUDE_LINK_H_

#include "list.h"

typedef enum {
    LINK_FILE,
    LINK_MQTT,
} link_type;

typedef struct link_output {
    void (*write)(struct link_output *output, const void *buf, size_t len);
    void (*write_char)(struct link_output *output, char data);
    void (*printf)(struct link_output *output, const char *fmt, ...);
    void (*free)(struct link_output *output);
} link_output_t;

typedef struct link {
    link_type type;
    char name[64];
    link_output_t *(*create_output)(struct link *link);
    void (*free)(list_t *links, struct link *link);
} link_t;

/// Construct a file link
link_t *link_file_create(list_t *links, const char *name, const char *file);

/// Construct a MQTT link
link_t *link_mqtt_create(list_t *links, const char *name, const char *host, const char *port, const char *user, const char *pass, const char *client_id);

#endif // INCLUDE_LINK_H_
