/*******************************************************************************
* Copyright 2020-2022 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/


#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>
#include <immintrin.h> // <-- to have AMX intrinsics
// {
// #ifdef _WIN32
// 	#ifndef WIN32_LEAN_AND_MEAN
// 		#define WIN32_LEAN_AND_MEAN
// 	#endif
// 	#include <windows.h>
// 	#include <malloc.h>
// 	#ifdef _MSC_VER
// 		#define XBYAK_TLS __declspec(thread)
// 	#else
// 		#define XBYAK_TLS __thread
// 	#endif
// #elif defined(__GNUC__)
// 	#include <unistd.h>
// 	#include <sys/mman.h>
// 	#include <stdlib.h>
// 	#define XBYAK_TLS __thread
// #endif
// }
namespace {
#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>

#define XFEATURE_XTILECFG 17
#define XFEATURE_XTILEDATA 18
#define XFEATURE_MASK_XTILECFG (1 << XFEATURE_XTILECFG)
#define XFEATURE_MASK_XTILEDATA (1 << XFEATURE_XTILEDATA)
#define XFEATURE_MASK_XTILE (XFEATURE_MASK_XTILECFG | XFEATURE_MASK_XTILEDATA)
#define ARCH_GET_XCOMP_PERM 0x1022
#define ARCH_REQ_XCOMP_PERM 0x1023

bool init() {
    unsigned long bitmask = 0;
    long status = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_PERM, &bitmask);
    if (0 != status) return false;
    if (bitmask & XFEATURE_MASK_XTILEDATA) return true;

    status = syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA);
    if (0 != status)
        return false; // XFEATURE_XTILEDATA setup is failed, TMUL usage is not allowed
    status = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_PERM, &bitmask);

    // XFEATURE_XTILEDATA setup is failed, can't use TMUL
    if (0 != status || !(bitmask & XFEATURE_MASK_XTILEDATA)) return false;

    // XFEATURE_XTILEDATA set successfully, TMUL usage is allowed
    return true;
}
#else
bool init() {
    return true;
}
#endif
}

// Struct to configure memory layout
//Byte(s)  Field Name                   Description
//0        palette                      Palette selects the supported configuration of the tiles that will be used.
//1        start_row                    start_row is used for storing the restart values for interrupted operations.
//2-15     reserved, must be zero       
//16-17    tile0.colsb                  Tile 0 bytes per row.
//18-19    tile1.colsb                  Tile 1 bytes per row.
//20-21    tile2.colsb                  Tile 2 bytes per row.
//...      (sequence continues)         
//30-31    tile7.colsb                  Tile 7 bytes per row.
//32-47    reserved, must be zero       
//48       tile0.rows                   Tile 0 rows.
//49       tile1.rows                   Tile 1 rows.
//50       tile2.rows                   Tile 2 rows.
//...      (sequence continues)
//55       tile7.rows                    Tile 7 rows.
//56-63    reserved, must be zero
         
#pragma pack(1)
struct amx_memory_layout {
  unsigned char palette = 1;  // Leaving those value undefined makes Segmentation fault
  unsigned char start_row = 0;
  unsigned char reserved[14] = {0};
  unsigned short tiles_bytes_per_row[8] = {64, 64, 64}; // Max availale ie.g. 64 bytes per tile's row
  unsigned short reserved2[8] = {0};
  unsigned char tiles_rows[8] = {16, 16, 16}; // Max availale ie.g. 16 rows per tile
  unsigned char reserved3[8] = {0};
};
#pragma pack(0)

void print_tile_buf(int8_t* tile_buf, size_t columns, size_t rows, const char* msg)
{
  printf("%s(rows=%lu,cols=%lu):\n",msg,rows,columns);
  // Tile is upto 64 bytes a row and upto 16 lines
  for (auto j=0; j< rows; ++j) {
    for (auto i=0; i< columns; ++i) {
      printf("%u ",tile_buf[columns*j+i]);
    }
    printf("\n");
  }  
}

void fill_tile_buf_ones(int8_t* tile_buf, size_t columns, size_t rows)
{
   for(auto i=0; i<columns*rows;++i) {
     tile_buf[i] = 1;
   }
}

void fill_tile_buf_twos(int8_t* tile_buf, size_t columns, size_t rows)
{
   for(auto i=0; i<columns*rows;++i) {
     tile_buf[i] = 2;
   }
}

void fill_tile_buf_inc(int8_t* tile_buf, size_t columns, size_t rows)
{
   for(auto i=0; i<columns*rows;++i) {
     tile_buf[i] = i%columns;
   }
}

int main(int argc, char **argv) {

  printf("Hello AMX intrinsics!!\n");

{
    // experiment: use system call to enable AMX
    puts("Using system call to enable AMX...");
    if (!init()) return 1;
    puts("...AMX is now enabled!");
}

{
    // debug: check tile config memory
    printf("sizeof(amx_memory_layout) = %zu\n", sizeof(amx_memory_layout));
}

 // 1. make a configuration
 amx_memory_layout cfg;

 // load configure tiles 
 _tile_loadconfig(&cfg);

{
    // debug: inspect each byte of tile configuration
    unsigned char x[64];
    _tile_storeconfig(&x);
    for (auto i = 0; i < 8; ++i) {
        printf("%hhu", x[i * 8 + 0]);
        for (auto j = 1; j < 8; ++j) {
            printf(",%hhu", x[i * 8 + j]);
        }
        printf("\n");
    }
}

//  _tile_loadd (tmm0, const void * base, int stride);

    const int tile_index = 0;
    printf("Calling tilezero on tmm%d...\n", tile_index);
  // Each tile is 64 bytes * 16 rows = 1024 bytes
  int8_t tile_buf[64*16*sizeof(int8_t)] = {0};
  int8_t tile_buf2[64*16*sizeof(int8_t)] = {0};
  int8_t tile_buf3[64*16*sizeof(int8_t)] = {0};

  _tile_zero(0);
  puts("...success!");
  _tile_stored(0, tile_buf, /*stride*/ 1);

  // Load data
  fill_tile_buf_ones(tile_buf2,64,16);
  fill_tile_buf_ones(tile_buf,64,16);

  _tile_loadd(1, tile_buf, /*stride*/ 64); // load data to tile. stride is bytes per line
  _tile_loadd(2, tile_buf2, /*stride*/ 64); // load data to tile. stride is bytes per line
                                            //
  print_tile_buf(tile_buf,64,16,"TMM0");
  print_tile_buf(tile_buf2,64,16,"TMM1");

  // Make a dot product
  // TODO: why result is like it is (how much columns is taken from tmm1 tile???) 
  _tile_dpbuud(0, 1, 2); // intput args are uint8_t  but result is dword value

  // read back tile_data
  _tile_stored(0, tile_buf2, /*stride*/ 64);
  print_tile_buf(tile_buf2,64,16,"Result: TMM0");



//LDTILECFG [rax]
//// assume some outer loops driving the cache tiling (not shown)
//{
//TILELOADD tmm0, [rsi+rdi] // srcdst, RSI points to C, RDI is strided value
//TILELOADD tmm1, [rsi+rdi+N] // second tile of C, unrolling in SIMD dimension N
//MOV r14, 0
//LOOP:
//TILELOADD tmm2, [r8+r9]
// // src2 is strided load of A, reused for 2 TMUL instr.
//TILELOADD tmm3, [r10+r11] // src1 is strided load of B
//TDPBUSD tmm0, tmm2, tmm3 // update left tile of C
//TILELOADD tmm3, [r10+r11+N] // src1 loaded with B from next rightmost tile
//TDPBUSD tmm1, tmm2, tmm3 // update right tile of C
//ADD r8, K
// // update pointers by constants known outside of loop
//ADD r10, K*r11
//ADD r14, K
//CMP r14, LIMIT
//JNE LOOP
//TILESTORED [rsi+rdi], tmm0 // update the C matrix in memory
//TILESTORED [rsi+rdi+M], tmm1
//} // end of outer loop
//TILERELEASE
//// return tiles to INIT state


  // __m128bh _mm_cvtne2ps_pbh (__m128 a, __m128 b); <-- conversion of fp32 data into bf16


    puts("calling tile release...");
  _tile_release();
}
