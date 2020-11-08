/**
 *
 * @author Sanzhar Kudaibergen
 * @created 10/30/2020
 */

#ifndef ALLOCATR_ALLOCATR_H
#define ALLOCATR_ALLOCATR_H

#include <stddef.h>

void *alloc(size_t size);

void dealloc(void *ptr);

#endif //ALLOCATR_ALLOCATR_H
