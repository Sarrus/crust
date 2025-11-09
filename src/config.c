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

#include <string.h>
#include <unistd.h>
#include "config.h"
#include "../libcyaml/include/cyaml/cyaml.h"

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
char crustOptionDaemonConfigFilePath[PATH_MAX] = "";

#ifdef NCURSES
bool crustOptionWindowEnterLog = false;
char crustOptionWindowConfigFilePath[PATH_MAX];
#endif

#ifdef GPIO
char crustOptionGPIOPath[PATH_MAX];
char * crustOptionPinMapString = NULL;
bool crustOptionInvertPinLogic = false;
#endif

static const cyaml_config_t cyamlConfig = {
    .log_fn = cyaml_log,
    .mem_fn = cyaml_mem,
    .log_level = CYAML_LOG_WARNING
};

void crust_config_load_defaults()
{
    strncpy(crustOptionRunDirectory, CRUST_RUN_DIRECTORY, PATH_MAX);
    strncpy(crustOptionSocketPath, CRUST_RUN_DIRECTORY, PATH_MAX);
    strncat(crustOptionSocketPath, CRUST_SOCKET_NAME, PATH_MAX - strlen(crustOptionSocketPath) - 1);
    crustOptionTargetUser = getuid();
    crustOptionTargetGroup = getgid();
}
