// compat_paths addresses following compatibility issue:
// topic: default search paths for config file
// issue: Linux and Windows use different common paths for config files
// solution: provide specific default paths for each system

#ifndef COMPAT_PATHS_INCLUDED
#define COMPAT_PATHS_INCLUDED

char **compat_getDefaultConfPaths();       // get default search paths for rtl_433 config file

#endif  // COMPAT_PATHS_INCLUDED
