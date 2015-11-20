#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <proto_manager.h>

static PT_ProtoOpr g_PT_ProtoOprHead;

int RegisterProtoOpr(PT_ProtoOpr ptProtoOpr)
{
	PT_ProtoOpr ptTmp;

	if(!g_PT_ProtoOprHead)
	{
		g_PT_ProtoOprHead = ptProtoOpr;
		ptProtoOpr->next = NULL;
	}else
	{
		ptTmp = g_PT_ProtoOprHead;

		while(ptTmp->next)
		{
			ptTmp = (PT_ProtoOpr)ptTmp->next;
		}

		ptTmp->next = ptProtoOpr;
		ptProtoOpr->next = NULL;
	}
}


PT_ProtoOpr GetProtoOpr(char *name)
{
	PT_ProtoOpr ptTmp = g_PT_ProtoOprHead;
	while(ptTmp)
	{
		if(strcmp(ptTmp->name, name) == 0)
		{
			return ptTmp;
		}
		ptTmp = (PT_ProtoOpr)ptTmp->next;
	}

	return NULL;
}

/**
  *	协议初始化
  */
int ProtoInit(void)
{
	int ret;
	ret = V2Init();
	return ret;
}












