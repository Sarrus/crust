#ifndef CRUST_OPTIONS_H
#define CRUST_OPTIONS_H
#include <stdbool.h>
#include <sys/types.h>
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

bool crustOptionVerbose;
enum crustRunMode crustOptionRunMode;
char crustOptionRunDirectory[PATH_MAX];
char crustOptionSocketPath[PATH_MAX];
char crustOptionGPIOPath[PATH_MAX];
bool crustOptionSetUser;
uid_t crustOptionTargetUser;
bool crustOptionSetGroup;
gid_t crustOptionTargetGroup;

#endif //CRUST_OPTIONS_H
