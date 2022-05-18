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

int main(int argc, char ** argv) {
    opterr = true;
    int option;
    while((option = getopt(argc, argv, "h")) != -1)
    {
        switch(option)
        {
            case 'h':
                fprintf(stderr, "CRUST: Consolidated Realtime Updates on Status of Trains\r\n");
                fprintf(stderr, "Usage: crust [options]\r\n");
                fprintf(stderr, "  -h  Display this help.\r\n");
                break;
        }
    }
    return EXIT_SUCCESS;
}
