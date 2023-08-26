#ifndef CRUST_OPTIONS_H
#define CRUST_OPTIONS_H
#include <stdbool.h>
#include <sys/types.h>
#include <netinet/in.h>
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
#define CRUST_DEFAULT_PORT 12321
#define CRUST_DEFAULT_IP_ADDRESS 0x100007f // 127.0.0.1

enum crustRunMode {
    CLI,
    DAEMON,
    NODE,
    WINDOW
};

extern bool crustOptionVerbose;
extern enum crustRunMode crustOptionRunMode;
extern char crustOptionRunDirectory[PATH_MAX];
extern char crustOptionSocketPath[PATH_MAX];
extern bool crustOptionSetUser;
extern uid_t crustOptionTargetUser;
extern bool crustOptionSetGroup;
extern gid_t crustOptionTargetGroup;
extern in_port_t crustOptionPort;
extern in_addr_t crustOptionIPAddress;

#ifdef GPIO
extern char crustOptionGPIOPath[PATH_MAX];
extern char * crustOptionPinMapString;
#endif

#endif //CRUST_OPTIONS_H
