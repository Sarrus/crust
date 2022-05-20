#ifndef CRUST_OPTIONS_H
#define CRUST_OPTIONS_H
#include <stdbool.h>
#include <sys/syslimits.h>

#define CRUST_DEFAULT_SOCKET_ADDRESS "/var/run/crust/crust.sock"
#define CRUST_DEFAULT_SOCKET_UMASK 0117

bool crustOptionVerbose;
bool crustOptionDaemon;
char crustOptionSocketPath[PATH_MAX];

#endif //CRUST_OPTIONS_H
