/******************************************************************************
 * Consolidated, Realtime Updates on Status of Trains (CRUST)
 * Copyright (C) 2022-2024 Michael R. Bell <michael@black-dragon.io>
 *
 * This file is part of CRUST. For more information, visit
 * <https://github.com/Sarrus/crust>
 *
 * CRUST is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * CRUST is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * CRUST. If not, see <https://www.gnu.org/licenses/>.
 *****************************************************************************/

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
#include "config.h"
#include "terminal.h"
#ifdef NCURSES
#include "window.h"
#endif
#ifdef GPIO
#include "node.h"
#endif
#ifdef MACOS
#include <uuid/uuid.h>
#endif

int main(int argc, char ** argv) {
#ifdef TESTING
    crust_terminal_print("WARNING: CRUST has been compiled with -DWITH_TESTING. This compile option enables insecure "
                         "functionality and should NEVER be used in production. Stay safe out there.");
#endif

    crust_config_load_defaults();

    struct passwd * userInfo = NULL;
    struct group * groupInfo = NULL;

    unsigned long prospectivePort = 0;
    struct in_addr prospectiveIPAddress;
    char * endPointer;

    opterr = true;
    int option;
    while((option = getopt(argc, argv, "a:dg:hilm:n:p:r:u:vw:")) != -1)
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
                crust_terminal_print("  -i  Invert the logic of the GPIO pins. "
                                     "(High = clear instead of high = occupied.)");
                crust_terminal_print("  -l  If running in window mode, start into the log screen.");
                crust_terminal_print("  -m  Specify track circuit to GPIO mapping in the format "
                                     "pin_number:circuit_number,[...]");
                crust_terminal_print("  -n  Run in node mode. Takes the path to a GPIO chip as an argument.");
                crust_terminal_print("  -p  Port of the CRUST server (defaults to 12321)");
                crust_terminal_print("  -r  Specify the run directory used to hold the CRUST socket. ");
                crust_terminal_print("  -u  Switch to this user after completing setup. "
                                     "(Only works if starting as root.)");
                crust_terminal_print("  -v  Display verbose output.");
                crust_terminal_print("  -w  Run in window mode. (Show a live view of the line.) Takes the "
                                     "path of a window layout file as an argument.");
                exit(EXIT_SUCCESS);

#ifdef GPIO
            case 'i':
                crustOptionInvertPinLogic = true;
                break;
#endif
#ifdef NCURSES
            case 'l':
                crustOptionWindowEnterLog = true;
#endif
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
#ifdef NCURSES
                crustOptionRunMode = CRUST_RUN_MODE_WINDOW;
                strncpy(crustOptionWindowConfigFilePath, optarg, PATH_MAX);
                crustOptionWindowConfigFilePath[PATH_MAX - 1] = '\0';
#else
                crust_terminal_print("CRUST only supports window mode when compiled with WITH_NCURSES set.");
                exit(EXIT_FAILURE);
#endif
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
#ifdef NCURSES
        case CRUST_RUN_MODE_WINDOW:
            crust_window_run();
#endif
    }

    return EXIT_SUCCESS;
}
