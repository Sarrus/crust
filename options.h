#ifndef CRUST_OPTIONS_H
#define CRUST_OPTIONS_H
#include <stdbool.h>
#ifdef MACOS
#include <sys/syslimits.h>
#else
#include <limits.h>
#endif //MACOS

#define CRUST_DEFAULT_SOCKET_ADDRESS "/var/run/crust/crust.sock"
#define CRUST_DEFAULT_SOCKET_UMASK 0117
#define CRUST_SOCKET_QUEUE_LIMIT 4096

bool crustOptionVerbose;
bool crustOptionDaemon;
char crustOptionSocketPath[PATH_MAX];

#endif //CRUST_OPTIONS_H
