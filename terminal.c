#include <stdio.h>
#include "terminal.h"
#include "options.h"

void crust_terminal_print(const char * text)
{
    fprintf(stderr, "%s\r\n", text);
}

void crust_terminal_print_verbose(const char * text)
{
    if(crustOptionVerbose)
    {
        crust_terminal_print(text);
    }
}