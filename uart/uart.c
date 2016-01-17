#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

#include <pthread.h>

static pthread_t thread_uart;

//һ֡����64bytes
#define FRAME_BUF_LEN 64
#define FRAME_DATA_LEN 61
#define FRAME_HEAD_LEN 3

//proto type
#define PROTO_HB   0XAA
#define PROTO_FILE 0XBB
#define PROTO_CMD  0xCC

//CMD
#define CMD_GET "get"


/*****************************************************************
* ���ƣ�                    UART0_Open
* ���ܣ�                    �򿪴��ڲ����ش����豸�ļ�����
* ��ڲ�����           port :���ں�(ttyS0,ttyS1,ttyS2)
* ���ڲ�����            ��ȷ����Ϊ1�����󷵻�Ϊ0
*****************************************************************/
int UART_Open(int *fd, char* port)
{
    *fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (*fd < 0)
	{
	    printf("Can't Open Serial Port");
	    return -1;
	}
	//�жϴ��ڵ�״̬�Ƿ�Ϊ����״̬  
	if(fcntl(*fd, F_SETFL, 0) < 0)
	{
	   printf("fcntl failed!/n");
	   return -1;
	}
	
	return 1;
}


/******************************************************************* 
* ���ƣ�                UART0_Close 
* ���ܣ�                �رմ��ڲ����ش����豸�ļ����� 
* ��ڲ�����        fd    :�ļ�������     port :���ں�(ttyS0,ttyS1,ttyS2) 
* ���ڲ�����        void 
*******************************************************************/  
void UART_Close(int fd)  
{  
	close(fd);  
} 

/*
  * ������9600
  * 8N1
  *
*/
int UART_Init(int fd)
{	
	//�ָ�����״̬Ϊ����״̬
	struct termios tOpt; 
	tcgetattr(fd, &tOpt);

	//һ������������ѡ��
	tOpt.c_cflag |= CLOCAL | CREAD;
	
	//8N1
	tOpt.c_cflag &= ~PARENB;
	tOpt.c_cflag &= ~CSTOPB;
	tOpt.c_cflag &= ~CSIZE;
	tOpt.c_cflag |= CS8;

	//diable hardware flow-ctrl
	tOpt.c_cflag &= ~CRTSCTS;

	//ʹ��ԭʼģʽͨѶ
	tOpt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

	//disable soft flow-ctrl
	tOpt.c_iflag &= ~(IXON | IXOFF | IXANY);  

	//ԭʼģʽ���
	tOpt.c_oflag &= ~OPOST;

	//baud rates
	cfsetispeed(&tOpt,B9600);
	cfsetospeed(&tOpt,B9600);

	//����1���ֽڣ�50ms
	//tOpt.c_cc[VTIME] = 0;
	//tOpt.c_cc[VMIN] = 0;

	//tcflush(fd, TCIOFLUSH);
	
	tcsetattr(fd,TCSANOW,&tOpt);
	
	return 0;
}

int UART_Recv(int fd, unsigned char *recv_buf, int data_len)
{
	int fd_sel;
	int len;
	fd_set fd_read;

	FD_ZERO(&fd_read);
	FD_SET(fd, &fd_read);

	fd_sel = select(fd+1, &fd_read, NULL, NULL, NULL);

	if(fd_sel < 0)
	{
		perror("select");
		return -1;
	}else
	{
		len = read(fd, recv_buf, data_len);
		return len;
	}
}

int UART_Send(int fd, char *send_buf, int data_len)
{
	int len;

	len = write(fd, send_buf, data_len);

	if(len < 0){
		perror("write");
		return -1;
	}

	return len;
}

static char getTypeFrmBuf(char *buf)
{
	return buf[0];
}

static int getDataLenFrmBuf(char *buf)
{
	return (int)buf[2];
}

/*
  *	��buf��������򿽱���data�У�offset��data�Ŀ�ʼλ��
  *	����ֵ : ������ĳ���
  *
*/
static int getDataFrmBuf(char *buf, char *data, int offset)
{
	int i;
	int j = offset;
	int data_len = getDataLenFrmBuf(buf);

	for(i = FRAME_HEAD_LEN; i < FRAME_HEAD_LEN + data_len; i++)
	{
		data[j++] = buf[i];
	}

	return data_len;
}

static int data2file(char *data, int len)
{
	FILE *fp;
	
	if((fp = fopen(CONFIG_FILE, "w")) == NULL)
	{
		perror("write2file");
		return -1;
	}

	fputs(data, fp);

	fclose(fp);

	return 0;
}

/*
  *	����frame
  * 	@frame:  ���������֡
  *	@type:	֡�����ֶ�
  *	@seq:	֡���
  *	@data_len:	������ĳ���
  *	@data:	��������
  *
*/
static int makeFrame(char *frame, char type, char seq, char data_len, char *data)
{
	int i, cnt = 0;
	//֡����
	frame[cnt++] = type;

	//֡���
	frame[cnt++] = seq;

	//������ĳ���
	frame[cnt++] = data_len;

	//��������
	for(i = 0; i < data_len; i++)
	{
		frame[cnt++] = data[i];
	}

	return 0;
}

static int file2buf(char *fileName, char *buf)
{
	FILE *fp;
	char ch;
	int len = 0;
	if((fp = fopen(fileName, "r")) == NULL)
	{
		perror("fopen");
		return -1;
	}

	ch = fgetc(fp);

	while(!feof(fp))
	{
		buf[len++] = ch;
		ch = fgetc(fp);
	}

	fclose(fp);

	return len;
	
}

static char *strlwr(char *s)
{
	char *str;
	str = s;
	while(*str != '\0')
	{
		if(*str >= 'A' && *str <= 'Z')
		{
			*str += 'a' - 'A';
		}
		str++;
	}

	return s;
}

void *UARTMsgListener(void *arg)
{
	int UART_Fd = (int)arg;
	int len, ret, i, cnt;
	char recv_buf[FRAME_BUF_LEN] = {0};
	char send_buf[FRAME_BUF_LEN] = {0};
	char type;
	char file_data[1024] = {0};
	char cmd[1024] = {0};
	int offset = 0;
	unsigned char seq = 0;

	char dataInFrame[FRAME_DATA_LEN] = {0};
	
	fd_set fd_read;
	struct timeval time;

	char *tmp_file_data;
	
	
	while(1)
	{
		time.tv_sec = 3;  
    	time.tv_usec = 0;
		FD_ZERO(&fd_read);
		FD_SET(UART_Fd, &fd_read); 
		
		ret = select(UART_Fd+1, &fd_read, NULL, NULL, &time);
		
		if(ret == 0)	//��ʱ
		{
			//������
			memset(dataInFrame, 0XFF, FRAME_DATA_LEN);
			makeFrame(send_buf, PROTO_HB, seq++, FRAME_DATA_LEN, dataInFrame);
			if(seq == 256) seq = 0;
			if(UART_Send(UART_Fd, send_buf, FRAME_BUF_LEN) < 0)
			{
				printf("UART_Send error\n");
			}
			
		}else if(ret > 0)
		{
			//���������ݣ���ȡ����
			if((len = read(UART_Fd, recv_buf, FRAME_BUF_LEN)) < 0)
			{
				printf("read error\n");
				continue;
			}
			printf("data len: %d, data from uart: ", len);
			for(i = 0; i < len; i++)
			{
				printf("%02x ", recv_buf[i]);
			}
			printf("\n");

			type = getTypeFrmBuf(recv_buf);
			
			//�ļ�֡
			if(type == PROTO_FILE)
			{
				len = getDataLenFrmBuf(recv_buf);
				//�ļ����ݵ����һ֡
				if(len < FRAME_DATA_LEN)	//�ݶ��������򳤶�Ϊ64bytes
				{
					len = getDataFrmBuf(recv_buf, file_data, offset);
					offset += len;
					file_data[offset] = '\0';
					printf("offset: %d, recv-file content: %s\n", offset, file_data);

					data2file(file_data, offset);

					offset = 0;
					memset(file_data, 0, 1024);
				}else if(len == FRAME_DATA_LEN){
					len = getDataFrmBuf(recv_buf, file_data, offset);
					offset += FRAME_DATA_LEN;
				}
			}
			//����֡
			else if(type == PROTO_CMD){
				len = getDataFrmBuf(recv_buf, cmd, 0);
				cmd[len] = '\0';
				printf("cmd: %s\n", cmd);

				if(strcmp(strlwr(cmd), CMD_GET) == 0)
				{
					if((len = file2buf(CONFIG_FILE, file_data)) < 0){
						perror("file2buf");
						continue;
					}
					file_data[len] = '\0';
					printf("len: %d, send-file content: %s\n", len, file_data);
					
					tmp_file_data = file_data;
					cnt = ((len-1) / FRAME_DATA_LEN) + 1;
					/*
						���ļ����ݷ�װ��֡�У����ͳ�ȥ
						���ܶ���һ֡
					*/
					for(i = 0; i < cnt; i++)
					{
						memset(send_buf, 0, FRAME_BUF_LEN);
						tmp_file_data = file_data;
						tmp_file_data += FRAME_DATA_LEN * i;
						makeFrame(send_buf, PROTO_FILE, i, 
							len>=FRAME_DATA_LEN?FRAME_DATA_LEN:len, tmp_file_data);
						len -= FRAME_DATA_LEN;
						//����һ֡
						
						if(UART_Send(UART_Fd, send_buf, FRAME_BUF_LEN) < 0)
						{
							printf("UART_Send error\n");
							break;
						}
					}

					//�ļ�����������64bytes�����������ٶ෢һ��֡
					if(len == 0)
					{
						memset(send_buf, 0, FRAME_BUF_LEN);
						memset(dataInFrame, 0XFF, FRAME_DATA_LEN);
						makeFrame(send_buf, PROTO_FILE, i, 
							0, dataInFrame);
						if(UART_Send(UART_Fd, send_buf, FRAME_BUF_LEN) < 0)
						{
							printf("UART_Send error\n");
							break;
						}
					}
					
				}else{
					printf("unkown commad\n");
				}
			}
			else{
				printf("unkown frame type\n");
			}
			
		}
	}

	return NULL;
}

void startUARTMsgListener(int UART_Fd)
{
	pthread_create(&thread_uart, NULL, &UARTMsgListener, (void *)UART_Fd);
}

