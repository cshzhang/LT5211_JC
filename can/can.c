#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <net/if.h>

#define BUF_SIZE 1024

int can_init(int *socket_can, struct sockaddr_can *ptSockaddr_can)
{
	struct ifreq ifr;

	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;
	if((*socket_can = socket(family, type, proto)) < 0)
	{
		perror("socket");
		return -1;
	}

	//构建sockaddr_can
	ptSockaddr_can->can_family = family;
	strcpy(ifr.ifr_name, CAN_DEV_NAME);
	ioctl(*socket_can, SIOCGIFINDEX, &ifr);
	ptSockaddr_can->can_ifindex = ifr.ifr_ifindex;

	//绑定
	if(bind(*socket_can, (struct sockaddr *)ptSockaddr_can, sizeof(struct sockaddr_can)) < 0)
	{
		perror("bind");
		return -1;
	}

	return 0;
}

/*
	将struct can_frame转换成可读字符串，放入buf
	@buf_len: buf的长度
	@return 解析出来字符串的长度
*/
int parseCanFrame(struct can_frame *canFrame, char *buf, int buf_len)
{
	int len = 0;
	int i;
	//格式化id
	if(canFrame->can_id & CAN_EFF_FLAG){
		//扩展帧
		len = snprintf(buf, buf_len, "<0x%08x> ", canFrame->can_id & CAN_EFF_MASK);
	}
	else{
		//标准帧
		len = snprintf(buf, buf_len, "<0x%03x> ", canFrame->can_id & CAN_SFF_MASK);
	}

	//格式化DLC
	len += snprintf(buf+len, buf_len - len, "[%d] ", canFrame->can_dlc);

	//格式化数据
	for(i = 0; i < canFrame->can_dlc; i++)
	{
		len += snprintf(buf+len, buf_len - len, "%02x ", canFrame->data[i]);
	}

	buf[len++] = '\n';

	return len;
	
}


/* 
		   id         dlc      data
	buf : <0x123> [8]   11 22 33 44 55 66 00 04
*/
void getIdFrombuf(char *buf, char *id)
{
	for(; *buf != '<'; buf++);
	buf++;
	for(; *buf != '>'; buf++)
	{
		*id = *buf;
		id++;
	}
	*id = '\0';
}


/*
	buf : <0x123> [8]   11 22 33 44 55 66 00 04
*/
int canFrame2file(char *buf, char *filename)
{
	char absolute_path[BUF_SIZE];
	FILE *fp;
	/**
	  *	"a+":  按文本文件打开，读、追加写                                                                                                         
	  *
	  */
	memset(absolute_path, 0, BUF_SIZE);
	strcat(absolute_path, SAVE_PATH);
	strcat(absolute_path, filename);
	//printf("filename: %s\n", absolute_path);
	
	if((fp = fopen(absolute_path, "a+")) == NULL)
	{
		perror("write2file");
		return -1;
	}

	fputs(buf, fp);

	fclose(fp);

	return 0;
}



//make-data from can-frame
/*	
	1　轨道电路各设备数量
		8 bytes, 3th-byte,4th-byte,7th-byte,8th-byte used,  剩下的填0x00
		3th-byte: 表示2的数量
		4th-byte: 表示3的数量
		7th-byte: 表示5的数量
		8th-byte: 表示5的数量
		
	2　既有继电编码ZPW-2000A区段设备状态
		2 bytes,  衰耗器通讯中断：填充数据无效标志，0x8000
		
	3	ZPW-2000A分机设备状态
		1 bytes表示一个分机状态,0x01表示通讯正常,0x00表示终端
		
	4	轨道电路特征量
		4.1 主轨道状态、小轨道状态
			1 bytes, 0x1b表示均无效
		4.2 轨道电路模拟量(27个模拟量)
			54 bytes
			
	5	报警信息区
		5 bytes
*/

/*
	@FJ_data:     长度4 * 8
	@SH_data:    长度5 * 8
	@FXP_data:   长度12 * 8

	@return:  dataInFrame的长度
*/
int makeDataInFrame(char *FJ_data, char *SH_data, 
							char *FXP_data, char *dataInFrame)
{

	int len = 0;
	int i;
	
	//FJ
	for(i = 0; i < 4*8; i++)
	{
		dataInFrame[len++] = FJ_data[i];
	}

	//SH
	for(i = 0; i < 5*8; i++)
	{
		dataInFrame[len++] = SH_data[i];
	}

	//FXP
	for(i = 0; i < 12*8; i++)
	{
		dataInFrame[len++] = SH_data[i];
	}


	return len;
	
}



