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
                       //
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

// As noted above, the kernel is able to control which processes are able to use the AMX instructions. The first step for a user-space process would be to use a new arch_prctl() command (ARCH_GET_XCOMP_SUPP) to get a list of supported features; if the appropriate bit is set in the result, AMX is available. Then, another arch_prctl() command (ARCH_REQ_XCOMP_PERM) can be used to request permission to use AMX. Some checks are made (one to be described shortly), and there is an opportunity for security modules to express an opinion as well. Normally, though, the request will be granted. Permissions apply to all threads in a process and are carried over a fork; calling execve() will reset them, though.

//One challenge presented by AMX is that processors can create a great deal of internal state while the AMX instructions are running. If the CPU is interrupted in the middle of an operation, that state must be saved somewhere or a lot of work could be lost. So, if a process is using AMX, the kernel must be prepared to save up to about 10KB of data in its interrupt handlers before doing much of anything else. This saving is done using the XSAVE instruction.

//The kernel allocates memory for each process that can be used for this purpose. Allocating 10KB for every process in the system would waste a lot of memory, though; most processes will never use AMX instructions. Happily, the processor can be configured to trap into the kernel the first time any process executes an AMX instruction; the kernel can then check whether permission to use those instructions has been granted and, if so, allocate an appropriately sized buffer to hold the FPU state and allow the operation to continue. 


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
         

//1,0,0,0,0,0,0,0
//0,0,0,0,0,0,0,0
//4,0,4,0,4,0,0,0
//0,0,0,0,0,0,0,0
//0,0,0,0,0,0,0,0
//0,0,0,0,0,0,0,0
//4,4,4,0,0,0,0,0
//0,0,0,0,0,0,0,0

// Working 
// bytes_pre_row: 4,4,4  tiles_rows: 2,2,1
// bytes_pre_row: 4,4,4  tiles_rows: 3,3,1

// Not working
// 2x1  : 2x4  * 4x1
// 3x2  : 3x8  * 2x4
#define t0_bytes_per_row 8   // N (4x due to dword)
#define t1_bytes_per_row 8   // K
#define t2_bytes_per_row 8   // 
#define t0_rows 3 // M
#define t1_rows 3 // M
#define t2_rows 2 // 

// Divide bytes per row values by four to get actual M,N,K


#pragma pack(1)
struct amx_memory_layout {
  unsigned char palette = 1;  // Leaving those value undefined makes Segmentation fault
  unsigned char start_row = 0;
  unsigned char reserved[14] = {0};
  unsigned short tiles_bytes_per_row[8] = {t0_bytes_per_row, t1_bytes_per_row, t2_bytes_per_row}; // Max availale ie.g. 64 bytes per tile's row
  unsigned short reserved2[8] = {0};
  unsigned char tiles_rows[8] = {t0_rows,t1_rows,t2_rows}; // Max availale ie.g. 16 rows per tile
  unsigned char reserved3[8] = {0};
};
#pragma pack(0)

void print_tile_buf_d(unsigned int* tile_buf, size_t rows, size_t bytes_per_row, const char* msg)
{
  auto columns = bytes_per_row/4;
  printf("%s(rows=%lu,cols=%lu):\n",msg,rows,columns);
  // Tile is upto 64 bytes a row and upto 16 lines
  for (unsigned int  j=0; j< rows; ++j) {
    for (unsigned int i=0; i< columns; ++i) {
      printf("%u ",tile_buf[columns*j+i]);
    }
    printf("\n");
  }  
}
void print_tile_buf(int8_t* tile_buf, size_t rows, size_t columns, const char* msg)
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

void fill_tile_buf_ones(int8_t* tile_buf, size_t rows, size_t columns)
{
   for(auto i=0; i<columns*rows;++i) {
     tile_buf[i] = 1;
   }
}

void fill_tile_buf_twos(int8_t* tile_buf, size_t rows, size_t columns)
{
   for(auto i=0; i<columns*rows;++i) {
     tile_buf[i] = 2;
   }
}

void fill_tile_buf_inc(int8_t* tile_buf, size_t rows, size_t columns)
{
   for(auto i=0; i<columns*rows;++i) {
     tile_buf[i] = i%columns;
   }
}

void fill_tile_buf_inc_row(int8_t* tile_buf, size_t rows, size_t columns)
{
   for(auto j=0; j<rows; ++j) {
     for(auto i=0; i<columns;++i) {
       tile_buf[j*columns+i] = j+1;
     }
   }
}

int main(int argc, char **argv) {

  printf("Hello AMX intrinsics!!\n");

{
    // experiment: use system call to enable AMX
    puts("Using system call to enable AMX...");
    if (!init()) { 
      printf("Error: AMX is not available\n");
      return 1;
    }
    puts("...AMX is now enabled!\n");
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
  fill_tile_buf_inc(tile_buf,t1_rows,t1_bytes_per_row);
  fill_tile_buf_inc(tile_buf2,t2_rows,t2_bytes_per_row);

  _tile_loadd(1, tile_buf, /*stride*/ t1_bytes_per_row); // load data to tile. stride is bytes per line
  _tile_loadd(2, tile_buf2, /*stride*/ t2_bytes_per_row); // load data to tile. stride is bytes per line
                                            //
  print_tile_buf(tile_buf,t1_rows,t1_bytes_per_row,"TMM1");
  print_tile_buf(tile_buf2,t2_rows,t2_bytes_per_row,"TMM2");

  // Make a dot product
  // TODO: why result is like it is (how much columns is taken from tmm1 tile???) 
  _tile_dpbuud(0, 1, 2); // intput args are uint8_t  but result is dword value

  // read back tile_data
  _tile_stored(0, tile_buf2, /*stride*/ t0_bytes_per_row);
  print_tile_buf_d(reinterpret_cast<unsigned int*>(tile_buf2),t0_rows,t0_bytes_per_row,"Result: TMM0");

  puts("calling tile release...");
  _tile_release();
}
