#include <libgen.h>
#include <string.h>
#include <stdio.h>

#include "log/log.h"
#include "cmd.h"
#include "version_git.h"
#include "config/config.h"

void printUsage(char *programName)
{
    printf("%s [OPTIONS]\n", basename(programName));
    printf("Required Options:\n");
    printf("  --binary           | -b  Specifies the ELF file to run (required)\n");
    printf("Options:\n");
    printf("  --version          | -v  Displays the version of the KittenTools\n");
    printf("  --help             | -h  Displays this usage text\n");
    printf("  --create           | -C  Creates a default config.ini file\n");
}

void printVersion()
{
    printf("KittenTools %s\n", GIT_VERSION);
}

int parseArgs(int argc, char *argv[], char *command, char *gameELF)
{
    (void)command;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--binary") == 0)
        {
            gameELF[0] = '\0';
            for (int j = i + 1; j < argc; j++)
            {
                if (j > i + 1)
                {
                    strncat(gameELF, " ", MAX_PATH_LENGTH - strlen(gameELF) - 1);
                }
                strncat(gameELF, argv[j], MAX_PATH_LENGTH - strlen(gameELF) - 1);
            }
            return 0;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
        {
            printVersion();
            return PARSE_ARGS_HELP;
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            printUsage(argv[0]);
            return PARSE_ARGS_HELP;
        }
        else if (strcmp(argv[i], "-C") == 0 || strcmp(argv[i], "--create") == 0)
        {
            save_current_config(true);
            return PARSE_ARGS_HELP;
        }
        else
        {
            log_error(plfBoot, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return -1;
        }
    }

    return 0;
}