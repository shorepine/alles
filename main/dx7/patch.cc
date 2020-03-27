/*
 * Copyright 2013 Google Inc.
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

#include <cstring>
#include <stdio.h>
#include "patch.h"

void UnpackPatch(const char bulk[128], char patch[156]) {
	for (int op = 0; op < 6; op++) {
		// eg rate and level, brk pt, depth, scaling
		memcpy(patch + op * 21, bulk + op * 17, 11);
		char leftrightcurves = bulk[op * 17 + 11];
		patch[op * 21 + 11] = leftrightcurves & 3;
		patch[op * 21 + 12] = (leftrightcurves >> 2) & 3;
		char detune_rs = bulk[op * 17 + 12];
		patch[op * 21 + 13] = detune_rs & 7;
		patch[op * 21 + 20] = detune_rs >> 3;
		char kvs_ams = bulk[op * 17 + 13];
		patch[op * 21 + 14] = kvs_ams & 3;
		patch[op * 21 + 15] = kvs_ams >> 2;
		patch[op * 21 + 16] = bulk[op * 17 + 14];  // output level
		char fcoarse_mode = bulk[op * 17 + 15];
		patch[op * 21 + 17] = fcoarse_mode & 1;
		patch[op * 21 + 18] = fcoarse_mode >> 1;
		patch[op * 21 + 19] = bulk[op * 17 + 16];  // fine freq
	}
	memcpy(patch + 126, bulk + 102, 9);  // pitch env, algo
	char oks_fb = bulk[111];
	patch[135] = oks_fb & 7;
	patch[136] = oks_fb >> 3;
	memcpy(patch + 137, bulk + 112, 4);  // lfo
	char lpms_lfw_lks = bulk[116];
	patch[141] = lpms_lfw_lks & 1;
	patch[142] = (lpms_lfw_lks >> 1) & 7;
	patch[143] = lpms_lfw_lks >> 4;
	memcpy(patch + 144, bulk + 117, 11);  // transpose, name
	patch[155] = 0x3f;  // operator on/off

	// Confirm the parameters are within range
	CheckPatch(patch);

}
int clamped =0 ;
int file_clamped =0 ;

char clamp(char byte, int pos, char max) {
	if(byte > max) {
		clamped++;
		//printf("file %d clamped %d pos %d was %d is %d\n", file_clamped, clamped, pos, byte, max);
		return max;
	}
	return byte;
}

// Helpful, from
// http://homepages.abdn.ac.uk/d.j.benson/dx7/sysex-format.txt
// Note, 1% of my downloaded voices go well outside these ranges . 
// a TODO is check what happens when we slightly go outside 

const char max[] = {
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,	// osc6
	3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc5
	3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc4
	3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc3
	3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc2
	3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
	99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc1
	3, 3, 7, 3, 7, 99, 1, 31, 99, 14,

	99, 99, 99, 99, 99, 99, 99, 99, // pitch eg rate & level 
	31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48, // algorithm etc
	126, 126, 126, 126, 126, 126, 126, 126, 126, 126 // name
};

void CheckPatch(char patch[156]) {
	for(int i=0;i<155;i++) {
		patch[i] = clamp(patch[i], i, max[i]);
	}
	if(clamped) file_clamped++;
	clamped = 0;
}