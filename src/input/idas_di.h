#include "backend.h"
#include <windows.h>
HRESULT idas_di_init(const struct idas_io_backend **backend);
HRESULT idas_di_config_apply();
bool idas_di_read_buttons(uint8_t buttons[128]);
bool idas_di_read_shifter_buttons(uint8_t buttons[128]);