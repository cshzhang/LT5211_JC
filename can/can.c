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

	//����sockaddr_can
	ptSockaddr_can->can_family = family;
	strcpy(ifr.ifr_name, CAN_DEV_NAME);
	ioctl(*socket_can, SIOCGIFINDEX, &ifr);
	ptSockaddr_can->can_ifindex = ifr.ifr_ifindex;

	//��
	if(bind(*socket_can, (struct sockaddr *)ptSockaddr_can, sizeof(struct sockaddr_can)) < 0)
	{
		perror("bind");
		return -1;
	}

	return 0;
}

/*
	��struct can_frameת���ɿɶ��ַ���������buf
	@buf_len: buf�ĳ���
	@return ���������ַ����ĳ���
*/
int parseCanFrame(struct can_frame *canFrame, char *buf, int buf_len)
{
	int len = 0;
	int i;
	//��ʽ��id
	if(canFrame->can_id & CAN_EFF_FLAG){
		//��չ֡
		len = snprintf(buf, buf_len, "<0x%08x> ", canFrame->can_id & CAN_EFF_MASK);
	}
	else{
		//��׼֡
		len = snprintf(buf, buf_len, "<0x%03x> ", canFrame->can_id & CAN_SFF_MASK);
	}

	//��ʽ��DLC
	len += snprintf(buf+len, buf_len - len, "[%d] ", canFrame->can_dlc);

	//��ʽ������
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
	  *	"a+":  ���ı��ļ��򿪣�����׷��д                                                                                                         
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
	1�������·���豸����
		8 bytes, 3th-byte,4th-byte,7th-byte,8th-byte used,  ʣ�µ���0x00
		3th-byte: ��ʾ2������
		4th-byte: ��ʾ3������
		7th-byte: ��ʾ5������
		8th-byte: ��ʾ5������
		
	2�����м̵����ZPW-2000A�����豸״̬
		2 bytes,  ˥����ͨѶ�жϣ����������Ч��־��0x8000
		
	3	ZPW-2000A�ֻ��豸״̬
		1 bytes��ʾһ���ֻ�״̬,0x01��ʾͨѶ����,0x00��ʾ�ն�
		
	4	�����·������
		4.1 �����״̬��С���״̬
			1 bytes, 0x1b��ʾ����Ч
		4.2 �����·ģ����(27��ģ����)
			54 bytes
			
	5	������Ϣ��
		5 bytes
*/

/*
	@FJ_data:     ����4 * 8
	@SH_data:    ����5 * 8
	@FXP_data:   ����12 * 8

	@return:  dataInFrame�ĳ���
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



