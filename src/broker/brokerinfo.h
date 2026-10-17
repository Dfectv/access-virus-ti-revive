// brokerinfo.h - the broker's INFO message (VTIMSG_INFO, after a client's HELLO): "key=value" fields separated by
// tabs, for now its release version - the plug-in shows it in PATCH UTILITY > INFO where the original shows the Access
// driver versions (E189). TIREVIVE_VERSION comes from the build (Makefile: make VERSION=0.8.21), else "dev".
#pragma once
#include <stdio.h>

#ifndef TIREVIVE_VERSION
#define TIREVIVE_VERSION "dev"
#endif

static inline int vti_info_text(char *buf, int n)
{
    return snprintf(buf, (size_t)n, "version=%s", TIREVIVE_VERSION);
}
