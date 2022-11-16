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
  unsigned short tiles_bytes_per_row[8] = {64}; // Max availale ie.g. 64 bytes per tile's row
  unsigned char reserved2[16] = {0};
  unsigned char tiles_rows[8] = {16}; // Max availale ie.g. 16 rows per tile
  unsigned char reserved3[8] = {0};
};
#pragma pack(0)

int main(int argc, char **argv) {

  printf("Hello AMX intrinsics!!\n");

 // 1. make a configuration
 amx_memory_layout cfg;
 // configure tiles 
 _tile_loadconfig(&cfg);

//  _tile_loadd (tmm0, const void * base, int stride);

  // Each tile is 64 bytes * 16 rows = 1024 bytes
  int8_t tile_buf[64*16*sizeof(int8_t)] = {2};

  _tile_stored(0, tile_buf, /*stride*/ 1);


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


  // 2. Load tiles of data
  // void _tile_loadd (__tile dst, const void * base, int stride);
  // void _tile_zero (__tile tdest)
  
  // __m128bh _mm_cvtne2ps_pbh (__m128 a, __m128 b); <-- conversion of fp32 data into bf16


//AMX-TILE:ldtilecfg/sttilecfg/tileloadd/tileloaddt1/tilezero/tilerelease
//AMX-INT8:tdpbssd/tdpbsud/tdpbusd/tdpbuud
//AMX-BF16:tdpbf16ps
  //auto y = _tile_dpbssd();
  //auto tz = __tilezero();

  // 3. Do dot product

  // 4. Get data back

  // 5. Release the tiles configuration
  _tile_release();
}

