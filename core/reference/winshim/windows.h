/* The Linux build of the headless reference. Cpu_68k.c and Cpu_Z80.c include windows.h for
 * declarations of headers they also include. Nothing from Windows is called. */
#ifndef CHIMERA_GENS_WINSHIM_WINDOWS_H
#define CHIMERA_GENS_WINSHIM_WINDOWS_H

#include <stdlib.h>
#include <string.h>

typedef void *HWND;
typedef void *HANDLE;
typedef void *HINSTANCE;
typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef long LPARAM;
typedef unsigned int WPARAM;

#endif