#pragma once

#include <stddef.h>
#include <stdint.h>

struct linux_termios
{
    unsigned int c_iflag;  
    unsigned int c_oflag;  
    unsigned int c_cflag;   
    unsigned int c_lflag;   
    unsigned char c_line;   
    unsigned char c_cc[32]; 
    unsigned int c_ispeed;  
    unsigned int c_ospeed;  
};

void termiosBridge_initBridges(void);