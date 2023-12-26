
#ifndef SYS_SINE_H__
#define SYS_SINE_H__

#include <stdint.h>
#include "sys.h"

const __attribute__((__common__)) uint8_t sine_tbl[ORIG_W];
const __attribute__((__common__)) uint8_t cos_tbl_sys[256];
const __attribute__((__common__)) uint8_t sin_tbl_sys[256];

#endif
