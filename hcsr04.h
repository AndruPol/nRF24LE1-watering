/*
 * hcsr04.h
 *
 *  Created on: 11 июля 2016 г.
 *      Author: andru
 */

#ifndef HCSR04_H_
#define HCSR04_H_

typedef enum {
	HCSR04_OK = 0,
	HCSR04_TIMEOUT,
	HCSR04_MAX,
} hcsr04_state_t;

void  hcsr04_init(void);
hcsr04_state_t hcsr04_read(uint16_t *range);

#endif /* HCSR04_H_ */
