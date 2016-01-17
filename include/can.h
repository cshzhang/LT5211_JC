#ifndef _CAN_H
#define _CAN_H

int can_init(int *socket_can, struct sockaddr_can *ptSockaddr_can);
int parseCanFrame(struct can_frame *canFrame, char *buf, int buf_len);
int makeDataInFrame(char *FJ_data, char *SH_data, char *FXP_data, char *dataInFrame);
void getIdFrombuf(char *buf, char *id);
int canFrame2file(char *buf, char *filename);


#endif

