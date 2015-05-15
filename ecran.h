#ifndef ECRAN_H
#define ECRAN_H

#include <stdint.h>

int la_init_ecran();

void la_lcdHome();
void la_lcdClear();
void la_lcdPosition(int col, int row);
void la_lcdPutChar(uint8_t c);
void la_lcdPuts(char* str);
//void la_lcdPrintf(char* msg, ...);

void la_exit();

#endif        //  #ifndef ECRAN_H
