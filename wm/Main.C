#include <unistd.h>
#include <locale.h>

#include "Manager.h"

#include "Client.h"
#include "Border.h"
#include "Config.h"

int main(int argc, char **argv)
{
    int i;

#ifndef VERSION
#define VERSION "0.1.0"
#endif
    if (argc > 1) {
	if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v")) {
	    printf("wm2tj " VERSION "\n");
	    return 0;
	}
	for (i = strlen(argv[0])-1; i > 0 && argv[0][i] != '/'; --i);
	fprintf(stderr, "usage: %s [--version]\n", argv[0] + (i > 0) + i);
	exit(2);
    }

    wm2LoadConfig();

    // character type only, for typing Japanese into the menu's search
    // row through the input method (XMODIFIERS); numbers etc. stay C
    setlocale(LC_CTYPE, "");

    {
	WindowManager manager;
    }

    // SIGHUP: restart in place (same PID, so the login session survives).
    // Reloads ~/.wm2rc and picks up a newly installed binary.
    if (WindowManager::m_restart) {
	execvp(argv[0], argv);
	perror("wm2tj: restart failed");
	return 1;
    }
    return 0;
}

