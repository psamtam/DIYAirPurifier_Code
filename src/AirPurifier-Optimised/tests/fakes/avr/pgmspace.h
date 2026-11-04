#pragma once
#include <string.h>
#define PROGMEM
#define PSTR(s) s
typedef const char *PGM_P;
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define memcpy_P(a,b,n) memcpy(a,b,n)
#define strlen_P(s) strlen(s)
#define strcpy_P(a,b) strcpy(a,b)
