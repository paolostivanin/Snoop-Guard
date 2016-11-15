#include <stdio.h>
#include <unistd.h>
#include "webmonit.h"

void show_help (void);


int
main (int argc, char **argv)
{
    // TODO replace with https://developer.gnome.org/glib/stable/glib-Commandline-option-parser.html
    int opt;
    while ((opt = getopt (argc, argv, "c:sh")) != -1) {
        switch (opt) {
            case 'c':
                client_mode ();
            case 's':
                server_mode ();
            case 'h':
            default:
                show_help ();
                return -1;
        }
    }
    return 0;
}


void
show_help ()
{
    fprintf (stderr, "%s v%s developed by %s <%s>.\n"
            "A *wrong* option was specified. Available options are\n"
            "\t-c to run the program in client mode\n"
            "\t-s to run the program in server mode\n", SW_NAME, SW_VERSION, DEV_NAME, DEV_EMAIL);
}