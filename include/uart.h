#ifndef _UART_H
#define _UART_H

int UART_Init(int fd);
int UART_Open(int *fd, char* port);
void UART_Close(int fd);
int UART_Init(int fd);
void startUARTMsgListener(int UART_Fd);


#endif

