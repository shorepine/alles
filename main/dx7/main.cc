/*
 * Copyright 2012 Google Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
#include <iostream>
#include <cstdlib>
#include <math.h>
#include "synth.h"
#include "module.h"
#include "aligned_buf.h"
#include "freqlut.h"
#include "wavout.h"
#include "sawtooth.h"
#include "sin.h"
#include "exp2.h"
#include "log2.h"
#include "resofilter.h"
#include "fm_core.h"
#include "fm_op_kernel.h"
#include "env.h"
#include "patch.h"
#include "controllers.h"
#include "dx7note.h"

using namespace std;
extern int file_clamped;

void mkdx7notes(double sample_rate) {
  const int n_samples = 10 * 1024 ;

  Dx7Note note;
  FILE *f = fopen("../compact.bin","rb");
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  char * all_patches = (char*)malloc(fsize);
  fseek(f,0,SEEK_SET);
  fread(all_patches, 1, fsize, f);
  fclose(f);
  int patches = fsize / 128;
  printf("%d patches\n", patches);
  char *unpacked_patches = (char*) malloc(patches*156);
  for(int i=0;i<patches;i++) {
    UnpackPatch(all_patches + (i*128), unpacked_patches + (i*156));
  }
  printf("%d had to be clamped\n", file_clamped);
  WavOut w("/tmp/foo.wav", sample_rate, n_samples*patches);

  for(int patchy = 0;patchy<patches;patchy++) {
    note.init(unpacked_patches+(patchy*156), 50 + (patchy%12) , 100);
    Controllers controllers;
    controllers.values_[kControllerPitch] = 0x2000;
    int32_t buf[N];

    for (int i = 0; i < n_samples; i += N) {
      for (int j = 0; j < N; j++) {
        buf[j] = 0;
      }
      if (i >= n_samples * (7./8.)) {
        note.keyup();
      }
       note.compute(buf, 0, 0, &controllers);
      for (int j = 0; j < N; j++) {
        buf[j] >>= 2;
      }
      w.write_data(buf, N);
    }
  }
  w.close();
  free(all_patches);
  free(unpacked_patches);
}


int main(int argc, char **argv) {
  double sample_rate = 44100.0;
  Freqlut::init(sample_rate);
  Sawtooth::init(sample_rate);
  Sin::init();
  Exp2::init();
  Log2::init();
  mkdx7notes(sample_rate);
  return 0;
}
*/
