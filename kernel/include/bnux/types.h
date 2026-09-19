#ifndef BNUX_TYPES_H
#define BNUX_TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint64_t           size_t;
typedef uint64_t           uintptr_t;
typedef int64_t            ptrdiff_t;

#define NULL ((void*)0)
#define bool  int
#define true  1
#define false 0

#define PACKED __attribute__((packed))
#define ALIGN(x) __attribute__((aligned(x)))

#endif
