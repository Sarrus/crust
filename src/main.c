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
#include <netinet/in.h>
#include <arpa/inet.h>
#include "daemon.h"
#include "options.h"
#include "terminal.h"
#include "window.h"
#ifdef GPIO
#include "node.h"
#endif
#ifdef MACOS
#include <uuid/uuid.h>
#endif

bool crustOptionVerbose = false;
enum crustRunMode crustOptionRunMode = CRUST_RUN_MODE_CLI;
char crustOptionRunDirectory[PATH_MAX];
char crustOptionSocketPath[PATH_MAX];
bool crustOptionSetUser = false;
uid_t crustOptionTargetUser;
bool crustOptionSetGroup = false;
gid_t crustOptionTargetGroup;
in_port_t crustOptionPort = CRUST_DEFAULT_PORT;
in_addr_t crustOptionIPAddress = CRUST_DEFAULT_IP_ADDRESS;
bool crustOptionWindowEnterLog = false;

#ifdef GPIO
char crustOptionGPIOPath[PATH_MAX];
char * crustOptionPinMapString = NULL;
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

    struct passwd * userInfo = NULL;
    struct group * groupInfo = NULL;

    unsigned long prospectivePort = 0;
    struct in_addr prospectiveIPAddress;
    char * endPointer;

    opterr = true;
    int option;
    while((option = getopt(argc, argv, "a:dg:hlm:n:p:r:u:vw")) != -1)
    {
        switch(option)
        {
            case 'a':
                if(!inet_aton(optarg, &prospectiveIPAddress))
                {
                    crust_terminal_print("Invalid IP address specified");
                    exit(EXIT_FAILURE);
                }
                crustOptionIPAddress = prospectiveIPAddress.s_addr;
                break;

            case 'd':
                crustOptionRunMode = CRUST_RUN_MODE_DAEMON;
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
                crust_terminal_print("  -a  IP address of the CRUST server (defaults to 127.0.0.1)");
                crust_terminal_print("  -d  Run in daemon mode.");
                crust_terminal_print("  -g  Switch to this group after completing setup (if run as root) and set this "
                                     "group on the CRUST run directory. "
                                     "(Defaults to the primary group of the user specified by -u.)");
                crust_terminal_print("  -h  Display this help.");
                crust_terminal_print("  -l  If running in window mode, start into the log screen.");
                crust_terminal_print("  -m  Specify track circuit to GPIO mapping in the format "
                                     "pin_number:circuit_number,[...]");
                crust_terminal_print("  -n  Run in node mode. Takes the path to a GPIO chip as an argument.");
                crust_terminal_print("  -p  Port of the CRUST server (defaults to 12321)");
                crust_terminal_print("  -r  Specify the run directory used to hold the CRUST socket. ");
                crust_terminal_print("  -u  Switch to this user after completing setup. "
                                     "(Only works if starting as root.)");
                crust_terminal_print("  -v  Display verbose output.");
                crust_terminal_print("  -w  Run in window mode. (Show a live view of the line.)");
                exit(EXIT_SUCCESS);

            case 'l':
                crustOptionWindowEnterLog = true;

            case 'm':
#ifdef GPIO
                crustOptionPinMapString = optarg;
#endif
                break;

            case 'n':
#ifdef GPIO
                crustOptionRunMode = CRUST_RUN_MODE_NODE;
                strncpy(crustOptionGPIOPath, optarg, PATH_MAX - 1);
                crustOptionGPIOPath[PATH_MAX - 1] = '\0';
#else
                crust_terminal_print("CRUST only supports node mode when compiled with WITH_GPIO set.");
                exit(EXIT_FAILURE);
#endif
                break;

            case 'p':
                endPointer = optarg;
                prospectivePort = strtoul(optarg, &endPointer, 10);
                if(*optarg == '\0'
                    || *endPointer != '\0'
                    || prospectivePort > 65535
                    || !prospectivePort)
                {
                    crust_terminal_print("Invalid port specified");
                    exit(EXIT_FAILURE);
                }
                crustOptionPort = (in_port_t)prospectivePort;
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

            case 'w':
                crustOptionRunMode = CRUST_RUN_MODE_WINDOW;
                break;

            case '?':
            default:
                exit(EXIT_FAILURE);
        }
    }

    switch(crustOptionRunMode)
    {
        default:
        case CRUST_RUN_MODE_CLI:
            break;

        case CRUST_RUN_MODE_DAEMON:
            crust_daemon_run();

#ifdef GPIO
        case CRUST_RUN_MODE_NODE:
            crust_node_run();
#endif

        case CRUST_RUN_MODE_WINDOW:
            crust_window_run();
    }

    return EXIT_SUCCESS;
}
