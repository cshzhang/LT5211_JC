#ifndef _PROTO_MANAGER_H
#define _PROTO_MANAGER_H
#include <config.h>

typedef struct ProtoOpr{
	char *name;
	int (*ProtoInit)();
	int (*MakeFrame)(int send_seq, int ack_seq, int frame_type, char *frame_data, int data_len, char *result);
	int (*GetFrameType)(char *frame, int frame_len); 	 
	int (*GetFrameSendSeq)(char *frame, int frame_len); 
	int (*GetFrameAckSeq)(char *frame, int frame_len);
	struct T_ProtoOpr *next;
}T_ProtoOpr, *PT_ProtoOpr;

int RegisterProtoOpr(PT_ProtoOpr ptProtoOpr);
PT_ProtoOpr GetProtoOpr(char *name);
int ProtoInit(void);
int V2Init(void);

#endif