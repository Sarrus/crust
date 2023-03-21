/*
 * CRUST: Consolidated Realtime Updates on Status of Trains
 *
 * CRUST is an application for monitoring the status of trains on light / heritage railways. It aims to provide very fast
 * updates as trains progress through their timetables. This is the C executable which functions as either the CRUST
 * command line tool or the CRUST daemon.
 *
 * The CRUST command line tool allows modification of a CRUST deployment by communicating directly with the CRUST daemon.
 *
 * The CRUST daemon is the fulcrum of a CRUST deployment. It holds railway state information in memory and records it in
 * a database. It also accepts status updates from CRUST writers and dispatches them to CRUST listeners.
 *
 * The CRUST daemon inherently trusts all communication it receives. To properly secure an installation, the CRUST API
 * must be positioned between the CRUST daemon and the outside world.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "daemon.h"
#include "options.h"
#include "terminal.h"
#ifdef GPIO
#include "node.h"
#endif
#ifdef MACOS
#include <uuid/uuid.h>
#endif

bool crustOptionVerbose = false;
enum crustRunMode crustOptionRunMode = CLI;
char crustOptionRunDirectory[PATH_MAX];
char crustOptionSocketPath[PATH_MAX];
bool crustOptionSetUser = false;
uid_t crustOptionTargetUser;
bool crustOptionSetGroup = false;
gid_t crustOptionTargetGroup;

#ifdef GPIO

char crustOptionGPIOPath[PATH_MAX];
CRUST_GPIO_PIN_MAP * crustOptionPinMapStart = NULL;

CRUST_GPIO_PIN_MAP * crustParsePinMap(char * mapText)
{
    char * next, * current, * subNext;
    next = mapText;
    while((current = subNext = strsep(&next, ",")) != NULL)
    {
        if((current = strsep(&subNext, ":")) == NULL
            || *current == '\0'
            || subNext == NULL
            || *subNext == '\0')
        {
            crust_terminal_print("Invalid track circuit GPIO map");
            exit(EXIT_FAILURE);
        }

        errno = 0;
        unsigned long pinNumber = strtoul(current, NULL, 10);
        if(errno
            || pinNumber > UINT_MAX)
        {
            crust_terminal_print("Invalid track circuit GPIO map");
            exit(EXIT_FAILURE);
        }
        unsigned long trackCircuitNumber = strtoul(subNext, NULL, 10);
        if(errno
           || trackCircuitNumber > UINT_MAX)
        {
            crust_terminal_print("Invalid track circuit GPIO map");
            exit(EXIT_FAILURE);
        }

        printf("%lu %lu\r\n", pinNumber, trackCircuitNumber);
    }

    return NULL;
}
#endif

int main(int argc, char ** argv) {
#ifdef TESTING
    crust_terminal_print("WARNING: CRUST has been compiled with -DWITH_TESTING. This compile option enables insecure "
                         "functionality and should NEVER be used in production. Stay safe out there.");
#endif

    strncpy(crustOptionRunDirectory, CRUST_RUN_DIRECTORY, PATH_MAX);
    strncpy(crustOptionSocketPath, CRUST_RUN_DIRECTORY, PATH_MAX);
    strncat(crustOptionSocketPath, CRUST_SOCKET_NAME, PATH_MAX - strlen(crustOptionSocketPath) - 1);
    crustOptionTargetUser = getuid();
    crustOptionTargetGroup = getgid();

#ifdef GPIO
    crustOptionPinMapStart = NULL;
#endif

    struct passwd * userInfo = NULL;
    struct group * groupInfo = NULL;

    opterr = true;
    int option;
    while((option = getopt(argc, argv, "dg:hm:n:r:u:v")) != -1)
    {
        switch(option)
        {
            case 'd':
                crustOptionRunMode = DAEMON;
                break;

            case 'g':
                groupInfo = getgrnam(optarg);
                if(groupInfo == NULL)
                {
                    crust_terminal_print("Unrecognised group.");
                    exit(EXIT_FAILURE);
                }
                crustOptionSetGroup = true;
                crustOptionTargetGroup = groupInfo->gr_gid;
                break;

            case 'h':
                crust_terminal_print("CRUST: Consolidated Realtime Updates on Status of Trains");
                crust_terminal_print("Usage: crust [options]");
                crust_terminal_print("  -d  Run in daemon mode.");
                crust_terminal_print("  -g  Switch to this group after completing setup (if run as root) and set this "
                                     "group on the CRUST run directory. "
                                     "(Defaults to the primary group of the user specified by -u.)");
                crust_terminal_print("  -h  Display this help.");
                crust_terminal_print("  -m  Specify track circuit to GPIO mapping in the format "
                                     "pin_number:circuit_number,[...]");
                crust_terminal_print("  -n  Run in node mode. Takes the path to a GPIO chip as an argument.");
                crust_terminal_print("  -r  Specify the run directory used to hold the CRUST socket. ");
                crust_terminal_print("  -u  Switch to this user after completing setup. "
                                     "(Only works if starting as root.)");
                crust_terminal_print("  -v  Display verbose output.");
                exit(EXIT_SUCCESS);

            case 'm':
#ifdef GPIO
                crustParsePinMap(optarg);
#endif
                break;

            case 'n':
#ifdef GPIO
                crustOptionRunMode = NODE;
                strncpy(crustOptionGPIOPath, optarg, PATH_MAX - 1);
                crustOptionGPIOPath[PATH_MAX - 1] = '\0';
#else
                crust_terminal_print("CRUST only supports node mode when compiled with WITH_GPIO set.");
                exit(EXIT_FAILURE);
#endif
                break;

            case 'r':
                strncpy(crustOptionRunDirectory, optarg, PATH_MAX);
                crustOptionRunDirectory[PATH_MAX - 1] = '\0';
                size_t runDirectoryPathLength = strnlen(crustOptionRunDirectory, PATH_MAX - 1);
                if(crustOptionRunDirectory[runDirectoryPathLength - 1] != '/')
                {
                    crustOptionRunDirectory[runDirectoryPathLength] = '/';
                }
                strncpy(crustOptionSocketPath, crustOptionRunDirectory, PATH_MAX);
                strncat(crustOptionSocketPath, CRUST_SOCKET_NAME, PATH_MAX - strlen(crustOptionSocketPath) - 1);
                break;

            case 'u':
                userInfo = getpwnam(optarg);
                if(userInfo == NULL)
                {
                    crust_terminal_print("Unrecognised user.");
                    exit(EXIT_FAILURE);
                }
                crustOptionSetUser = true;
                crustOptionTargetUser = userInfo->pw_uid;
                if(!crustOptionSetGroup)
                {
                    crustOptionSetGroup = true;
                    crustOptionTargetGroup = userInfo->pw_gid;
                }
                break;

            case 'v':
                crustOptionVerbose = true;
                break;

            case '?':
            default:
                exit(EXIT_FAILURE);
        }
    }

    switch(crustOptionRunMode)
    {
        default:
        case CLI:
            break;

        case DAEMON:
            crust_daemon_run();

#ifdef GPIO
        case NODE:
            crust_node_run();
#endif
    }

    return EXIT_SUCCESS;
}
