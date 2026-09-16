

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PARSE_ARGS_SUCCESS 0
#define PARSE_ARGS_FAILURE -1
#define PARSE_ARGS_HELP 1

#define MAX_PATH_LENGTH 1024

int parseArgs(int argc, char *argv[], char *command, char *gameELF);
