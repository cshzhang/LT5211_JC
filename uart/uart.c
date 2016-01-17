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

//一帧长度64bytes
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
* 名称：                    UART0_Open
* 功能：                    打开串口并返回串口设备文件描述
* 入口参数：           port :串口号(ttyS0,ttyS1,ttyS2)
* 出口参数：            正确返回为1，错误返回为0
*****************************************************************/
int UART_Open(int *fd, char* port)
{
    *fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (*fd < 0)
	{
	    printf("Can't Open Serial Port");
	    return -1;
	}
	//判断串口的状态是否为阻塞状态  
	if(fcntl(*fd, F_SETFL, 0) < 0)
	{
	   printf("fcntl failed!/n");
	   return -1;
	}
	
	return 1;
}


/******************************************************************* 
* 名称：                UART0_Close 
* 功能：                关闭串口并返回串口设备文件描述 
* 入口参数：        fd    :文件描述符     port :串口号(ttyS0,ttyS1,ttyS2) 
* 出口参数：        void 
*******************************************************************/  
void UART_Close(int fd)  
{  
	close(fd);  
} 

/*
  * 波特率9600
  * 8N1
  *
*/
int UART_Init(int fd)
{	
	//恢复串口状态为阻塞状态
	struct termios tOpt; 
	tcgetattr(fd, &tOpt);

	//一般会添加这两个选项
	tOpt.c_cflag |= CLOCAL | CREAD;
	
	//8N1
	tOpt.c_cflag &= ~PARENB;
	tOpt.c_cflag &= ~CSTOPB;
	tOpt.c_cflag &= ~CSIZE;
	tOpt.c_cflag |= CS8;

	//diable hardware flow-ctrl
	tOpt.c_cflag &= ~CRTSCTS;

	//使用原始模式通讯
	tOpt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

	//disable soft flow-ctrl
	tOpt.c_iflag &= ~(IXON | IXOFF | IXANY);  

	//原始模式输出
	tOpt.c_oflag &= ~OPOST;

	//baud rates
	cfsetispeed(&tOpt,B9600);
	cfsetospeed(&tOpt,B9600);

	//最少1个字节，50ms
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
  *	将buf里的数据域拷贝到data中，offset是data的开始位置
  *	返回值 : 数据域的长度
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
  *	构造frame
  * 	@frame:  构造出来的帧
  *	@type:	帧类型字段
  *	@seq:	帧序号
  *	@data_len:	数据域的长度
  *	@data:	数据内容
  *
*/
static int makeFrame(char *frame, char type, char seq, char data_len, char *data)
{
	int i, cnt = 0;
	//帧类型
	frame[cnt++] = type;

	//帧序号
	frame[cnt++] = seq;

	//数据域的长度
	frame[cnt++] = data_len;

	//数据内容
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
		
		if(ret == 0)	//超时
		{
			//发心跳
			memset(dataInFrame, 0XFF, FRAME_DATA_LEN);
			makeFrame(send_buf, PROTO_HB, seq++, FRAME_DATA_LEN, dataInFrame);
			if(seq == 256) seq = 0;
			if(UART_Send(UART_Fd, send_buf, FRAME_BUF_LEN) < 0)
			{
				printf("UART_Send error\n");
			}
			
		}else if(ret > 0)
		{
			//串口有数据，读取出来
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
			
			//文件帧
			if(type == PROTO_FILE)
			{
				len = getDataLenFrmBuf(recv_buf);
				//文件数据的最后一帧
				if(len < FRAME_DATA_LEN)	//暂定，数据域长度为64bytes
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
			//命令帧
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
						将文件内容封装到帧中，发送出去
						可能多余一帧
					*/
					for(i = 0; i < cnt; i++)
					{
						memset(send_buf, 0, FRAME_BUF_LEN);
						tmp_file_data = file_data;
						tmp_file_data += FRAME_DATA_LEN * i;
						makeFrame(send_buf, PROTO_FILE, i, 
							len>=FRAME_DATA_LEN?FRAME_DATA_LEN:len, tmp_file_data);
						len -= FRAME_DATA_LEN;
						//发送一帧
						
						if(UART_Send(UART_Fd, send_buf, FRAME_BUF_LEN) < 0)
						{
							printf("UART_Send error\n");
							break;
						}
					}

					//文件长度正好是64bytes的整数倍，再多发一空帧
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

