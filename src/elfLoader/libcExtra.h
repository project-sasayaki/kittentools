#pragma once

void libcExtraBridge_initBridges(void);

void libcExtra_trace_cxx_throws(void);

/* Guest libstdc++ moves std::wstring contents through wmemmove. */
void libcExtra_guest_wide_strings(void);
