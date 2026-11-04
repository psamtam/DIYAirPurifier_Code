#pragma once
#include <string.h>
#define PROGMEM
#define PSTR(s) s
typedef const char *PGM_P;
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define strlen_P(s) strlen(s)
#define strcpy_P(a,b) strcpy(a,b)
