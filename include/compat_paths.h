// compat_paths addresses following compatibility issue:
// topic: default search paths for config file
// issue: Linux and Windows use different common paths for config files
// solution: provide specific default paths for each system

#ifndef INCLUDE_COMPAT_PATHS_H_
#define INCLUDE_COMPAT_PATHS_H_

/// get default search paths for rtl_433 config file
char **compat_get_default_conf_paths(void);

#endif  /* INCLUDE_COMPAT_PATHS_H_ */
