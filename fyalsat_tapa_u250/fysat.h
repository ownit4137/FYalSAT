#ifndef WSAT_HLS_H
#define WSAT_HLS_H

#include "ap_int.h"
#include <ap_fixed.h>
#include "hls_vector.h"
#include "hls_stream.h"
#include <iostream>
#include <fstream>
#include <string>

#include <tapa.h>
template <typename T>
using bits = ap_uint<tapa::widthof<T>()>;

typedef int cls; 
typedef short var; 
typedef short bscore; 
typedef char nlit;

typedef int length;
typedef short clength;

#define GETPOS(L) (2*ABS(L)-(L<0)-1)	// -1(0) 1(1) -2(2) 2(3) -3(4) 3(5)
#define ABS(a) (((a) < 0) ? (-a) : (a))

const int MAXFLIP =  10000000;
const int UCBSIZE =  32768;//20000; // multiple of 16
const int MAXK    =  64; // 64 // multiple of 16
//const int MAXR    =  65536;// multiple of 16
const int MAXNCLS =  630000; // multiple of 16
const int MAXNVAR =  2400; // multiple of 16
const int MAXNLIT =  2 * MAXNVAR;
const int SDSIZE  =  16;
const int CDSIZE  =  32;

const int DSIZE   =  16;
#define DSIZE_LOG 4
typedef ap_uint<11> ucbidx; //=log(UCBSIZE/DSIZE)
const int TPE_NUM = 4;
#define DO_UCB_PREFETCH
const int PREFETCH_NUM = 3;

//#define ENABLE_DBG_UCBNUM

#endif
