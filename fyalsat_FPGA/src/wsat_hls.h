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

// ../../sat_sw/2018_main/af750c18578d52e60472315692ad83c0-si2-b03m-m800-03.cnf
// nl 21724 | nc 472588 | maxo 21670 | avro 289.128567 | maxk 20

// ../../sat_sw/2018_main/a5364102dcc3cf5ba701c0bb8de675f2-Karatsuba7654321x1234567.cnf
// nl 1550 | nc 17253 | maxo 565 | avro 76.949032 | maxk 22, 119271
// numFalse: 345 totblks: 12949

// ../../sat_sw/2018_main/queen8-8-9.cnf
// nl 512 | nc 6744 | maxo 216 | avro 103.125000 | maxk 8, 52800
// numFalse: 728 totblks: 4685

// ../../sat_sw/2018_main/sted5_0x24204-60.cnf
// nl 4690 | nc 61866 | maxo 218 | avro 51.326652 | maxk 12, 240722
// numFalse: 6511 totblks: 26690

// ../../sat_sw/d_2022/31c4aacd83bfd2aa7a9762a5b463b992-MVD_ADS_S11_7_7.cnf
// nl 88 | nc 689652 | maxo 177102 | avro 54857.409091 | maxk 7, 4827452

// ../../sat_sw/d_2022/9c4892c9ac4e9f5d83d33b5210e7af4e-MVD_ADS_S6_6_5.cnf
// nl 112 | nc 336010 | maxo 33649 | avro 16201.517857 | maxk 6, 1814570

// ../../sat_sw/d_2022/cf5f4eb714cab56ce430af77d26e1a6e-MVD_ADS_S10_5_6.cnf
// nl 100 | nc 609319 | maxo 118759 | avro 36402.900000 | maxk 6, 3640290

// #define MAXFLIP 10000000
// #define UCBSIZE 60000 // multiple of 16
// #define MAXK 32 // 64 // multiple of 16
// #define MAXR 22000 // 640 // multiple of 16
// #define MAXNCLS 480000 // multiple of 16
// #define MAXNVAR 22000 // multiple of 16
// #define MAXNLIT 2 * MAXNVAR
// #define GETPOS(L) (2*ABS(L)-(L<0)-1)	// -1(0) 1(1) -2(2) 2(3) -3(4) 3(5)
// #define ABS(a) (((a) < 0) ? (-a) : (a))
// #define DSIZE 16
// #define SDSIZE 16
// #define CDSIZE 32

// #define MAXFLIP 10000000
// #define UCBSIZE 30000 // multiple of 16
// #define MAXK 16 // 64 // multiple of 16
// #define MAXR 14272 // 640 // multiple of 16
// #define MAXNCLS 630000 // multiple of 16
// #define MAXNVAR 2288 // multiple of 16
// #define MAXNLIT 2 * MAXNVAR
// #define GETPOS(L) (2*ABS(L)-(L<0)-1)	// -1(0) 1(1) -2(2) 2(3) -3(4) 3(5)
// #define ABS(a) (((a) < 0) ? (-a) : (a))
// #define DSIZE 16
// #define SDSIZE 16
// #define CDSIZE 32

#define MAXFLIP 10000000
#define UCBSIZE 20000 
#define MAXK 64 
#define MAXR 120000 
#define MAXNCLS 630000 
#define MAXNVAR 2400 
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



}
#endif
