// bytebeat.c
// for kyle

// t*((t>>12|t>>8)&63&t>>4)   // by viznut
// ((-t&4095)(255&t(t&t>>13))>>12)+(127&t*(234&t>>8&t>>3)>>(3&t>>14)) // by tejeez
// t*(t>>11&t>>8&123&t>>3)    // by tejeez
// t*((t>>9|t>>13)&25&t>>6)   // by visy
// (t*(t>>5|t>>8))>>(t>>16)   // by tejeez
// ((t*(t>>8|t>>9)&46&t>>8))^(t&t>>13|t>>6) // lost in space by xpansive
// ((t&4096)?((t*(t^t%255)|(t>>4))>>1):(t>>3)|((t&8192)?t<<2:t)) // by skurk (raer's version)
// (t>>7|t|t>>6)10+4(t&t>>13|t>>6) // by viznut, xpansive, varjohukka
// "t*5&(t>>7)|t*3&(t*4>>10)" // by miiro
// (t|(t>>9|t>>7))*t&(t>>11|t>>9) // by red
// v=(v>>1)+(v>>4)+t*(((t>>16)|(t>>6))&(69&(t>>9))) // by pyryp
// (t>>6|t|t>>(t>>16))*10+((t>>11)&7)  //by viznut
// (t*(4|7&t>>13)>>((~t>>11)&1)&128) + ((t)(t>>11&t>>13)((~t>>9)&3)&127) // by stimmer


#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include "exprtk.hpp"

//#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "freertos/queue.h"
//#include "freertos/event_groups.h"

//#include "esp_system.h"
//#include "esp_spi_flash.h"
//#include "esp_intr_alloc.h"
//#include "esp_attr.h"
//#include "esp_event.h"
//#include "esp_log.h"
//#include "esp_err.h"


struct mathop {
	uint8_t type;
	uint16_t l;
	uint16_t r;
};

struct bytebeat {
	uint16_t t;
	struct mathop * mathops;
	uint16_t num_mathops;
};

uint16_t render(struct bytebeat b) {

	return 0;
}

struct bytebeat parse(char * string, uint16_t len) {
	// parse a small form of C-dialect into some structure / function
	struct bytebeat b;
	b.t = 0;
	return b;
}