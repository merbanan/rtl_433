/** @file
    Light-weight (i.e. dumb) config-file parser.

    Copyright (C) 2018 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_CONFPARSE_H_
#define INCLUDE_CONFPARSE_H_

struct conf_keywords {
    char const *keyword;
    int key;
};

/** Check if a file exists and can be read.

    @param path input file name
    @return 1 if the file exists and is readable, 0 otherwise
*/
int hasconf(char const *path);

/** Open a config file, read contents to memory.

    @param path input file name
    @return allocated memory containing the config file
*/
char *readconf(char const *path);

/** Return the next keyword token and set the optional argument.

    @param conf current position in conf
    @param keywords list of possible keywords
    @param arg optional out pointer to a argument string
    @return the next keyword token, -1 otherwise.
*/
int getconf(char **conf, struct conf_keywords const keywords[], char **arg);

#endif /* INCLUDE_CONFPARSE_H_ */
