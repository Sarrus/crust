/******************************************************************************
 * Consolidated, Realtime Updates on Status of Trains (CRUST)
 * Copyright (C) 2022-2025 Michael R. Bell <michael@black-dragon.io>
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

#ifndef CRUST_CONFIG_H
#define CRUST_CONFIG_H
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
#define CRUST_DEFAULT_CONFIG_FILE "/etc/crust.yml"
#define CRUST_TCP_KEEPALIVE_INTERVAL 10
#define CRUST_TCP_MAX_FAILED_KEEPALIVES 3
#define CRUST_MAX_MESSAGE_LENGTH 256

enum crustRunMode {
    CRUST_RUN_MODE_CLI,
    CRUST_RUN_MODE_DAEMON,
    CRUST_RUN_MODE_NODE,
    CRUST_RUN_MODE_WINDOW
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
extern bool crustOptionWindowEnterLog;
extern char crustOptionWindowConfigFilePath[PATH_MAX];
extern char crustOptionDaemonConfigFilePath[PATH_MAX];

#ifdef GPIO
extern char crustOptionGPIOPath[PATH_MAX];
extern char * crustOptionPinMapString;
extern bool crustOptionInvertPinLogic;
#endif

void crust_config_load_defaults();

#endif //CRUST_CONFIG_H
