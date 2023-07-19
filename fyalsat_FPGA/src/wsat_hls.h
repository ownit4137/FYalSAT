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
typedef short ucbidx;

// typedef ap_uint<22> cls;
// typedef ap_int<20> var;
// typedef ushort bscore;
// typedef bool cost;
// typedef ushort length;
// typedef bool clength;
// typedef ap_int<19> ucbidx;

extern "C" {

#define MAXFLIP 10000000
#define UCBSIZE 30000 // multiple of 16
#define MAXK 32 // 64 // multiple of 16
#define MAXR 14272 // 640 // multiple of 16
#define MAXNCLS 630000 // multiple of 16
#define MAXNVAR 2288 // multiple of 16
#define MAXNLIT 2 * MAXNVAR
#define GETPOS(L) (2*ABS(L)-(L<0)-1)	// -1(0) 1(1) -2(2) 2(3) -3(4) 3(5)
#define ABS(a) (((a) < 0) ? (-a) : (a))
#define DSIZE 16
#define SDSIZE 16
#define CDSIZE 32

}
#endif
