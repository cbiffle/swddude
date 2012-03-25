#ifndef __sys_globals_h__
#define __sys_globals_h__

#include <stdint.h>
#include <stdbool.h>

#define null 0

typedef unsigned int	uint;
typedef uint8_t		uint8;
typedef uint16_t	uint16;
typedef uint32_t	uint32;

typedef int8_t		int8;
typedef int16_t		int16;
typedef int32_t		int32;

#define QUOTE_(m) #m
#define QUOTE(m) QUOTE_(m)

#endif //__sys_globlas_h__
