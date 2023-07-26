#ifndef WSAT_HLS_H
#define WSAT_HLS_H

#include "ap_int.h"
#include <ap_fixed.h>
#include "hls_vector.h"
#include "hls_stream.h"
#include <iostream>
#include <fstream>
#include <string>

// length data type change

typedef int cls; 
typedef int var; 
typedef int bscore; 
typedef char cost;
typedef int length;
typedef short clength;
typedef ushort ucbidx;

extern "C" {

#define MAXFLIP 10000000
#define UCBSIZE 30000 // multiple of 16
#define MAXK 16 // 64 // multiple of 16
#define MAXR 14272 // 640 // multiple of 16
#define MAXNCLS 630000 // multiple of 16
#define MAXNVAR 2288 // multiple of 16
#define MAXNLIT 2 * MAXNVAR
#define GETPOS(L) (2*ABS(L)-(L<0)-1)	// -1(0) 1(1) -2(2) 2(3) -3(4) 3(5)
#define ABS(a) (((a) < 0) ? (-a) : (a))
#define DSIZE 16
#define SDSIZE 16
#define CDSIZE 32

// #define MAXFLIP 100000000
// #define UCBSIZE 50000 // multiple of 16
// #define MAXK 64 // 64 // multiple of 16
// #define MAXR 512 // 640 // multiple of 16
// #define MAXNCLS 230000 // multiple of 16
// #define MAXNVAR 42000 // multiple of 16
// #define MAXNLIT 2 * MAXNVAR
// #define GETPOS(L) (2*ABS(L)-(L<0)-1)	// -1(0) 1(1) -2(2) 2(3) -3(4) 3(5)
// #define ABS(a) (((a) < 0) ? (-a) : (a))
// #define DSIZE 16
// #define SDSIZE 16
// #define CDSIZE 32
#define DSSIZE_C 1500000000
#define DSSIZE 70000000

}
#endif
