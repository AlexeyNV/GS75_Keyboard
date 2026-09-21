#ifndef MATRIX_H
#define MATRIX_H

#include "main.h"
#include <stdint.h>

#define MATRIX_LINES 26

void Matrix_Init(void);
void Matrix_Scan(void);

const uint32_t *Matrix_GetRaw(void);

#endif