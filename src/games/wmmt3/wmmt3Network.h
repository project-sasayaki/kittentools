#pragma once

#include <stdbool.h>

typedef struct
{
    unsigned char address[4];
    unsigned char mask[4];
    unsigned char mac[6];
    bool link;
} Wmmt3Interface;

/* Linked play: the game's cabinet networking runs on this module's Linux socket layer. */
void wmmt3_network_init(void);

/* The adapter the cabinet network uses, as the game's ifconfig.pl would report eth0. */
bool wmmt3_network_interface(Wmmt3Interface *out);
