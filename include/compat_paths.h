// compat_paths addresses following compatibility issue:
// topic: default search paths for config file
// issue: Linux and Windows use different common paths for config files
// solution: provide specific default paths for each system

#ifndef COMPAT_PATHS_H
#define COMPAT_PATHS_H

/// get default search paths for rtl_433 config file
char **compat_get_default_conf_paths();

#endif  // COMPAT_PATHS_H
