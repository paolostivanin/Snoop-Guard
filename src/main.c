#include <stdio.h>
#include <unistd.h>
#include "webmonit.h"s

void show_help (void);


int main (int argc, char **argv)
{
    int opt;
    while ((opt = getopt (argc, argv, "c:sh")) != -1) {
        switch (opt) {
            case 'c':
                client_mode ();
            case 's':
                server_mode ();
            default:
                show_help ();
        }
    }

    return 0;
}


void show_help ()
{
    fprintf (stderr, "");
}