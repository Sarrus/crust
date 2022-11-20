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
#include "daemon.h"
#include "options.h"
#include "terminal.h"
#ifdef MACOS
#include <uuid/uuid.h>
#endif

int main(int argc, char ** argv) {
#ifdef TESTING
    crust_terminal_print("WARNING: CRUST has been compiled with -DWITH_TESTING. This compile option enables insecure "
                         "functionality and should NEVER be used in production. Stay safe out there.");
#endif

    crustOptionVerbose = false;
    crustOptionDaemon = false;
    strcpy(crustOptionRunDirectory, CRUST_DEFAULT_RUN_DIRECTORY);
    crustOptionTargetUser = 0;
    crustOptionTargetGroup = 0;

    struct passwd * userInfo = NULL;
    struct group * groupInfo = NULL;

    opterr = true;
    int option;
    while((option = getopt(argc, argv, "dg:hr:u:v")) != -1)
    {
        switch(option)
        {
            case 'd':
                crustOptionDaemon = true;
                break;

            case 'g':
                groupInfo = getgrnam(optarg);
                if(groupInfo == NULL)
                {
                    crust_terminal_print("Unrecognised group.");
                    exit(EXIT_FAILURE);
                }
                crustOptionTargetGroup = groupInfo->gr_gid;
                break;

            case 'h':
                crust_terminal_print("CRUST: Consolidated Realtime Updates on Status of Trains");
                crust_terminal_print("Usage: crust [options]");
                crust_terminal_print("  -d  Run in daemon mode.");
                crust_terminal_print("  -g  Switch to this group after completing setup. "
                                     "(Defaults to the primary group of the user specified by -u.)");
                crust_terminal_print("  -h  Display this help.");
                crust_terminal_print("  -s  Define the address for the CRUST socket.");
                crust_terminal_print("  -u  Switch to this user after completing setup. "
                                     "(Only works if starting as root.)");
                crust_terminal_print("  -v  Display verbose output.");
                exit(EXIT_SUCCESS);

            case 'r':
                strncpy(crustOptionRunDirectory, optarg, PATH_MAX);
                crustOptionRunDirectory[PATH_MAX - 1] = '\0';
                break;

            case 'u':
                userInfo = getpwnam(optarg);
                if(userInfo == NULL)
                {
                    crust_terminal_print("Unrecognised user.");
                    exit(EXIT_FAILURE);
                }
                crustOptionTargetUser = userInfo->pw_uid;
                if(!crustOptionTargetGroup)
                {
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

    if((strlen(crustOptionRunDirectory) + strlen(CRUST_SOCKET_NAME)) > PATH_MAX)
    {
        crust_terminal_print("CRUST run directory path is too long.");
        exit(EXIT_FAILURE);
    }

    if(crustOptionDaemon)
    {
        crust_daemon_run();
    }

    return EXIT_SUCCESS;
}
