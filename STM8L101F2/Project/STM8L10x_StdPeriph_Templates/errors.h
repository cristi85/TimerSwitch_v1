#ifndef _ERRORS_H_
#define _ERRORS_H_

#include "board.h"

#define ERROR_FLASH_WRITE (u8)0

#define ERRORS (u8)ERROR_FLASH_WRITE + 1

void Errors_Init(void);
void Errors_SetError(u8);
void Errors_ResetError(u8);
bool Errors_CheckError(u8);
bool Errors_IsError(void);
u8 Errors_NumErrors(void);

#endif