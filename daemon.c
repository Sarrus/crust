#include <stdlib.h>
#include "daemon.h"
#include "terminal.h"
#include "state.h"

// Starts the CRUST daemon, exiting when the daemon finishes.
_Noreturn void crust_daemon_run()
{
    crust_terminal_print_verbose("Crust daemon starting...");
    crust_terminal_print_verbose("Building initial state...");

    CRUST_STATE * state;
    crust_state_init(&state);

    exit(EXIT_SUCCESS);
}