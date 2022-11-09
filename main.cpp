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

int main(int argc, char **argv) {

  printf("Hello AMX intrinsics!!\n");


  // 1. make a configuration
  _tile_loadconfig(/*const void * mem_addr*/nullptr);

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

