/**
 *
 * @author Sanzhar Kudaibergen
 * @created 11/10/2020
 */

#ifndef ALLOCATR_OSCORE_H
#define ALLOCATR_OSCORE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Request memory from OS
 */
void *request_mem(size_t size);

void alloc_lock_acquire();

void alloc_lock_release();

#endif //ALLOCATR_OSCORE_H
