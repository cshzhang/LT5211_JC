#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <linux/can.h>
#include <linux/can/raw.h>


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>

#include <proto_manager.h>

#include <config.h>
#include <v2.h>
#include <can.h>


#define BACKLOG 10
#define SERVER_PORT 9898
#define FD_SETCOUNT 32
#define BUF_SIZE 1024

static int running = 1;
static pthread_t thread_can;
PT_ProtoOpr pt_ProtoOpr;
static pthread_mutex_t multi_io_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_iSend_seq = 1;
static int g_iAck_seq = 0;


typedef struct MultiIOInfo {
	fd_set tReadSocketSet0;	//����
	fd_set tReadSocketSet1;	
	int count;				//��¼�ͻ��˵�����
	int max_fd;
	//����ͻ��˵�socket����
	//index = 0 �������ServerSocketFd
	int iSocketFds[FD_SETCOUNT];
}T_MultiIOInfo;
T_MultiIOInfo tMultiIOInfo;


static int max_fd(int fds[], int length);
static void PRINT_MULTIIO(T_MultiIOInfo *pTMulitIOInfo);
static int tcp_listen(int *socketServerFd, struct sockaddr_in *socketServerAddr);
static void startCanMsgListener(int socketFd);
static void removeSocketFdByIndex(int index);
static int lock();
static int unlock();
static void send2AllClients(char *data, int data_len);

/*
	��CAN�����Ͻ���CAN�����߳���
*/
void * CanMsgListener(void *arg)
{
	int socket_can = (int)arg;
	int i;
	struct can_frame canFrame;
	int nBytes;
	char buf[BUF_SIZE] = {0};
	char frame_out[BUF_SIZE] = {0};
	char dataInFrame[BUF_SIZE] = {0};
	int len, dataLen, v2_framelength;
	int num_FJ = 0, num_SH = 0, num_FXP = 0;

	//4֡
	char FJ_data[8 * 4] = {0};
	//5֡
	char SH_data[8 * 5] = {0};
	//12֡
	char FXP_data[8 *12] = {0};

	memset(FJ_data, 0xff, 8*4);
	memset(SH_data, 0xff, 8*5);
	memset(FXP_data, 0xff, 8*12);
	
	while(1)
	{
		memset(buf, 0, BUF_SIZE);
		//��can�����϶�ȡ����
		nBytes = read(socket_can, &canFrame, sizeof(struct can_frame));
		if(nBytes < 0)
		{
			perror("CAN_read");
			return NULL;
		}

		//len: ����������buf�ĳ���
		len = parseCanFrame(&canFrame, buf, BUF_SIZE);

		DBG_PRINTF("data from can-bus: %s", buf);

		
		//�������� 		��������		֡��
		//-------------------------------------------------
		//����			00000B				4
		//˥��			00001B				5
		//������			00010B				12
		//-------------------------------------------------

		if(canFrame.can_id == 0x123 && num_FJ < 4)	//����,4֡
		{
			for(i = 0; i < canFrame.can_dlc; i++)
			{
				FJ_data[num_FJ * 8 + i] = canFrame.data[i];
			}
			num_FJ++;
		}

		if(canFrame.can_id == 0x124 && num_SH < 5)	//˥��,5֡
		{
			for(i = 0; i < canFrame.can_dlc; i++)
			{
				SH_data[num_SH * 8 + i] = canFrame.data[i];
			}
			num_SH++;
		}

		if(canFrame.can_id == 0x125 && num_FXP < 12)	//������,12֡
		{
			for(i = 0; i < canFrame.can_dlc; i++)
			{
				FXP_data[num_FXP * 8 + i] = canFrame.data[i];
			}
			num_FXP++;
		}

		//send frame to control-center
		if(num_FJ == 4 && num_SH == 5 && num_FXP == 12)
		{
			num_FJ = 0;
			num_SH = 0;
			num_FXP = 0;

			memset(dataInFrame, 0xff, BUF_SIZE);
			dataLen = makeDataInFrame(FJ_data, SH_data, FXP_data, dataInFrame);
			
			//(int send_seq, int ack_seq, int frame_type, char *frame_data, int data_len, char *result)
			v2_framelength = pt_ProtoOpr->MakeFrame(g_iSend_seq, 
							g_iAck_seq, 
							FRAME_TYPE_TCD, 
							dataInFrame, 
							dataLen, 
							frame_out);
			lock();
			send2AllClients(frame_out, v2_framelength);
			g_iSend_seq++;
			g_iSend_seq = g_iSend_seq > 240 ? 1 : g_iSend_seq;
			unlock();
			//˯250ms, Ҫ��ʵʱ����Ϊ250ms
			usleep(1000 * 250);
			
			memset(FJ_data, 0xff, 8*4);
			memset(SH_data, 0xff, 8*5);
			memset(FXP_data, 0xff, 8*12);
		}

		/*
		lock();
		for(i = 1; i < FD_SETCOUNT; i++)
		{
			if(tMultiIOInfo.iSocketFds[i] != 0)
			{
				DBG_PRINTF("send frame to client, client-fd: %d\n", tMultiIOInfo.iSocketFds[i]);
				if(send(tMultiIOInfo.iSocketFds[i], buf, len, 0) <= 0){
					perror("send");
					removeSocketFdByIndex(i);
				}
			}
		}
		unlock();
		*/
	}
}


int main(int argc, char **argv)
{
	int i,ret;

	int HBCount = 0;

	struct sockaddr_in socketServerAddr;
	struct sockaddr_in socketClientAddr;
	int socketServerFd;
	int socketClientFd;

	int addrLen;
	char recvbuf[BUF_SIZE] = {0};  
	char sendbuf[BUF_SIZE] = {0};  
	int recv_len;
	int serverframe_type;
	char frame_data[BUF_SIZE] = {0};
	int v2_framelenth;

	int socket_can;
	struct sockaddr_can ptSockaddr_can;

	//��ʼ��T_MultiIOInfo
	tMultiIOInfo.count = 0;
	memset(tMultiIOInfo.iSocketFds, 0, FD_SETCOUNT * sizeof(int));

	//Э���ʼ��
	ProtoInit();
	pt_ProtoOpr = GetProtoOpr("V2");

	//�����CAN, socket, bind
	ret = can_init(&socket_can, &ptSockaddr_can);
	if(ret < 0)
	{
		perror("can_init");
		return -1;
	}
	//�첽���շ���������
	startCanMsgListener(socket_can);

	/* 
		socket -> bind -> listen
	*/
	ret = tcp_listen(&socketServerFd, &socketServerAddr);
	if(ret == -1)
	{
		perror("tcp_listen");
		return -1;
	}
	
	//��·����IOģ��
	FD_ZERO(&tMultiIOInfo.tReadSocketSet0);
	FD_SET(socketServerFd, &tMultiIOInfo.tReadSocketSet0);
	tMultiIOInfo.iSocketFds[0] = socketServerFd;
	tMultiIOInfo.max_fd = max_fd(tMultiIOInfo.iSocketFds, FD_SETCOUNT);
	
	while(running)
	{
		PRINT_MULTIIO(&tMultiIOInfo);
		
		FD_ZERO(&tMultiIOInfo.tReadSocketSet1);
		//�ָ����ݵ�fdset
		tMultiIOInfo.tReadSocketSet1 = tMultiIOInfo.tReadSocketSet0;

		//�ȴ��ܿػ������ӣ��Լ�v2.0����֡
		ret = select(tMultiIOInfo.max_fd + 1, &tMultiIOInfo.tReadSocketSet1, 
			NULL, NULL, NULL);
		if(ret < 0)
		{
			perror("select");
		}else
		{
			//�Ƿ���client����
			if(FD_ISSET(tMultiIOInfo.iSocketFds[0], &tMultiIOInfo.tReadSocketSet1) && 
				tMultiIOInfo.count < FD_SETCOUNT - 1)
			{
				addrLen = sizeof(struct sockaddr);
				socketClientFd = accept(tMultiIOInfo.iSocketFds[0], 
						(struct sockaddr *)&socketClientAddr, &addrLen);
				DBG_PRINTF("client connect, client ip: %s\n", 
						inet_ntoa(socketClientAddr.sin_addr));
				if(-1 == socketClientFd)
				{
					perror("accept");
				}else
				{
					lock();
					for(i = 1; i < FD_SETCOUNT; i++)
					{
						//��һ��fd=0 ��λ�ã���ӽ�ȥ
						if(tMultiIOInfo.iSocketFds[i] == 0)
						{
							tMultiIOInfo.iSocketFds[i] = socketClientFd;
							FD_SET(socketClientFd, &tMultiIOInfo.tReadSocketSet0);
							tMultiIOInfo.count++;
							tMultiIOInfo.max_fd = max_fd(tMultiIOInfo.iSocketFds, FD_SETCOUNT);
							break;
						}
					}
					unlock();
				}
				
			}

			for(i = 1; i < FD_SETCOUNT; i++)
			{
				//�����ݿɶ�
				if(FD_ISSET(tMultiIOInfo.iSocketFds[i], 
						&tMultiIOInfo.tReadSocketSet1))
				{
					memset(recvbuf, 0, BUF_SIZE);
					if((recv_len = recv(tMultiIOInfo.iSocketFds[i], 
							recvbuf, BUF_SIZE, 0)) <= 0)
					{
						DBG_PRINTF("client disconnect, fd: %d\n", tMultiIOInfo.iSocketFds[i]);
						lock();
						removeSocketFdByIndex(i);
						unlock();
						continue;
					}
					
					//��������������������
					int send_seq ;
					serverframe_type = pt_ProtoOpr->GetFrameType(recvbuf, recv_len);
					
					//�յ�����֡�������Ӧ��֡
					if(serverframe_type == FRAME_TYPE_CA)
					{
						//����ȫFF
						memset(frame_data, 0xff, BUF_SIZE);

						//����CR֡�����ظ�������
						v2_framelenth = pt_ProtoOpr->MakeFrame(0, 
												0, 
												FRAME_TYPE_CR, 
												frame_data, 
												10, 
												sendbuf);
						if(v2_framelenth == -1)
						{
							DBG_PRINTF("make-frame error\n");
							continue;
						}
						
						//����CRӦ��֡
						if(send(tMultiIOInfo.iSocketFds[i], sendbuf,
								v2_framelenth, 0) <= 0)
						{
							DBG_PRINTF("client disconnect, fd: %d\n", tMultiIOInfo.iSocketFds[i]);
							lock();
							removeSocketFdByIndex(i);
							unlock();
							continue;
						}
						
					}
					
					//�յ�����֡
					if(serverframe_type == FRAME_TYPE_HB)
					{
						//TODO..
						DBG_PRINTF("recv heart beat...%d\n", HBCount++);
						send_seq = pt_ProtoOpr->GetFrameSendSeq(recvbuf, recv_len);
						g_iAck_seq = send_seq;
					}

					//�յ���������
					//TODO
					
				}
			}

		}
		
	}
	
	return 0;
}



static void startCanMsgListener(int socketFd)
{
	pthread_create(&thread_can, NULL, &CanMsgListener, (void *)socketFd);
}


static int tcp_listen(int *socketServerFd, struct sockaddr_in *pt_socketServerAddr)
{
	int ret = 0;
	*socketServerFd = socket(AF_INET, SOCK_STREAM, 0);
	
	pt_socketServerAddr->sin_family 	 = AF_INET;
	pt_socketServerAddr->sin_port		 = htons(SERVER_PORT);	/* host to net, short */
	pt_socketServerAddr->sin_addr.s_addr = INADDR_ANY;
	memset(pt_socketServerAddr->sin_zero, 0, 8);

	ret = bind(*socketServerFd, (const struct sockaddr *)pt_socketServerAddr, sizeof(struct sockaddr));
	if (-1 == ret)
	{
		printf("bind error!\n");
		return -1;
	}

	ret = listen(*socketServerFd, BACKLOG);
	if (-1 == ret)
	{
		printf("listen error!\n");
		return -1;
	}

	return 0;
}



static int max_fd(int fds[], int length)
{
	int max = 0;
	int i;
	for(i = 0; i < length; i++)
	{
		if(max < fds[i])
		{
			max = fds[i];
		}
	}

	return max;
}


static void removeSocketFdByIndex(int index)
{
	FD_CLR(tMultiIOInfo.iSocketFds[index], &tMultiIOInfo.tReadSocketSet0);
	close(tMultiIOInfo.iSocketFds[index]);
	tMultiIOInfo.iSocketFds[index] = 0;
	tMultiIOInfo.count--;
	tMultiIOInfo.max_fd = max_fd(tMultiIOInfo.iSocketFds, FD_SETCOUNT);
}

static void PRINT_MULTIIO(T_MultiIOInfo *pTMulitIOInfo)
{
	int i = 0;
	int len = 0;
	char buf[BUF_SIZE] = {0};

	DBG_PRINTF("\n\n----------MultiIOInfo-----------\n");
	DBG_PRINTF("client numbers: %d\n", pTMulitIOInfo->count);
	DBG_PRINTF("max fd: %d\n", pTMulitIOInfo->max_fd);
	

	len = snprintf(buf, BUF_SIZE, "%s", "iSocketFds: ");
	for(i = 0; i < FD_SETCOUNT; i++)
	{
		len += snprintf(buf + len, BUF_SIZE - len, 
			"%d ", pTMulitIOInfo->iSocketFds[i]);
	}
	DBG_PRINTF("%s", buf);
	
	DBG_PRINTF("\n----------MultiIOInfo-----------\n\n");
}

static void send2AllClients(char *data, int data_len)
{
	int i;
	for(i = 1; i < FD_SETCOUNT; i++)
	{
		if(tMultiIOInfo.iSocketFds[i] != 0)
		{
			DBG_PRINTF("send frame to client, client-fd: %d\n", tMultiIOInfo.iSocketFds[i]);
			if(send(tMultiIOInfo.iSocketFds[i], data, data_len, 0) <= 0){
				perror("send");
				removeSocketFdByIndex(i);
			}
		}
	}
}

static int lock()
{ 
	return pthread_mutex_lock(&multi_io_lock);
}

static int unlock()
{
	return pthread_mutex_unlock(&multi_io_lock);
}

