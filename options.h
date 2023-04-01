#ifndef CRUST_OPTIONS_H
#define CRUST_OPTIONS_H
#include <stdbool.h>
#include <sys/types.h>
#include "state.h"
#ifdef MACOS
#include <sys/syslimits.h>
#else
#include <limits.h>
#endif //MACOS

#define CRUST_RUN_DIRECTORY "/var/run/crust/"
#define CRUST_SOCKET_NAME "crust.sock"
#define CRUST_DEFAULT_SOCKET_UMASK 0117
#define CRUST_SOCKET_QUEUE_LIMIT 4096

enum crustRunMode {
    CLI,
    DAEMON,
    NODE
};

extern bool crustOptionVerbose;
extern enum crustRunMode crustOptionRunMode;
extern char crustOptionRunDirectory[PATH_MAX];
extern char crustOptionSocketPath[PATH_MAX];
extern bool crustOptionSetUser;
extern uid_t crustOptionTargetUser;
extern bool crustOptionSetGroup;
extern gid_t crustOptionTargetGroup;

#ifdef GPIO
extern char crustOptionGPIOPath[PATH_MAX];
extern char * crustOptionPinMapString;
#endif

#endif //CRUST_OPTIONS_H
