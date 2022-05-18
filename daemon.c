#include <stdlib.h>
#include "daemon.h"
#include "terminal.h"

// Starts the CRUST daemon, exiting when the daemon finishes.
_Noreturn void crust_daemon_run()
{
    crust_terminal_print_verbose("Crust daemon starting...");
    exit(EXIT_SUCCESS);
}