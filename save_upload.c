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

#define BUF_SIZE 255

extern int optind, opterr, optopt;

static int sock = -1;
static char *path = "/etc/can_data/";
static int running = 1;
static pthread_t thread_server;

//和服务器通信的socket
static int g_socketFd;

void print_usage(char *arg)
{
	fprintf(stderr, "Usage: %s [can-interface]\n", arg);
}

static void onSignoReceived(int signo)
{
	running = 0;
}

static int canFrame2file(char *buf, char *filename)
{
	char absolute_path[BUF_SIZE];
	FILE *fp;
	/**
	  *	"a+":  按文本文件打开，读、追加写                                                                                                         
	  *
	  */
	memset(absolute_path, 0, BUF_SIZE);
	strcat(absolute_path, path);
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

#define SERVER_PORT 9898
static char *server_ip = "10.1.15.1";

static int connect2Server(int *sockfd, struct sockaddr_in *server_addr)
{
	int ret = -1;
	
	if((*sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0)
	{
		perror("socket");
		return -1;
	}
	//构造server_addr
	server_addr->sin_family = AF_INET;
	server_addr->sin_port   = htons(SERVER_PORT);
	inet_aton(server_ip, &server_addr->sin_addr);
	memset(server_addr->sin_zero, 0, 8);

	ret = connect(*sockfd, (struct sockaddr *)server_addr, sizeof(struct sockaddr));
	if(ret < 0)
	{
		perror("connect");
		return -1;
	}

	return 0;
}


void * ServerMsgListener(void *arg)
{
	int socketFd = (int)arg;
	int recv_len;
	char buf[BUF_SIZE];
	while(1)
	{
		memset(buf, 0, BUF_SIZE);
		recv_len = recv(socketFd, buf, BUF_SIZE, 0);
		if(recv_len < 0)
		{
			close(socketFd);
			return NULL;
		}

		//do work with msg from server...

		printf("get msg from server: %s", buf);
	}
}


static void startServerMsgListener(int socketFd)
{
	pthread_create(&thread_server, NULL, &ServerMsgListener, (void *)socketFd);
}

static void getIdFrombuf(char *buf, char *id)
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

int main(int argc, char **argv)
{
	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;
	struct sockaddr_can addr;
	struct ifreq ifr;
	struct can_frame canFrame;
	int nBytes;
	char buf[BUF_SIZE];
	int len;
	int i;
	int res = 0;
	char nodeId[64];

	//socket
	int sockfd;
	struct sockaddr_in server_addr;
	int send_length;

	if(argc == 1)
	{
		print_usage(argv[0]);
		exit(0);
	}

	//连接服务器
	if(res = connect2Server(&sockfd, &server_addr) < 0)
	{	
		perror("connect2Server");
		exit(0);
	}
	g_socketFd = sockfd;

	//异步接收服务器数据
	startServerMsgListener(sockfd);

	//注册信号
	signal(SIGTERM, onSignoReceived);
	signal(SIGHUP, onSignoReceived);

	printf("interface = %s, family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW", argv[optind]);

	//sokcet-CAN
	if((sock = socket(family, type, proto)) < 0)
	{
		perror("socket");
		return 1;
	}

	//构建sockaddr_can
	addr.can_family = family;
	strcpy(ifr.ifr_name, argv[optind]);
	ioctl(sock, SIOCGIFINDEX, &ifr);
	addr.can_ifindex = ifr.ifr_ifindex;

	//绑定
	if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		perror("bind");
		return 1;
	}

	printf("\nReceive msg form interface %s\n", ifr.ifr_name);

	while(running)
	{
		nBytes = read(sock, &canFrame, sizeof(struct can_frame));
		if(nBytes < 0){
			perror("read");
			return 1;
		}else{
			//清空buf
			memset(buf, 0, BUF_SIZE);
			//格式化ID 到buf
			if(canFrame.can_id & CAN_EFF_FLAG){
				//扩展帧
				len = snprintf(buf, BUF_SIZE, "<0x%08x> ", canFrame.can_id & CAN_EFF_MASK);
			}else{
				//标准帧
				len = snprintf(buf, BUF_SIZE, "<0x%03x> ", canFrame.can_id & CAN_SFF_MASK);
			}

			//格式化DLC 到buf
			len += snprintf(buf + len, BUF_SIZE - len, "[%d] ", canFrame.can_dlc);

			//格式化数据到buf
			for(i = 0; i < canFrame.can_dlc; i++){
				len += snprintf(buf + len, BUF_SIZE - len, "%02x ", canFrame.data[i]);
			}

			//判断是否是远程请求帧
			//TODO...

		}

		getIdFrombuf(buf, nodeId);

		//构造完毕，打印
		buf[len] = '\n';
		len++;
		printf("%s", buf);

		//写入文件
		if(res = canFrame2file(buf, nodeId) < 0)
		{
			perror("write2file");
		}

		//上传给服务器
		if((send_length = send(sockfd, buf, len, 0)) < 0)
		{
			perror("send");
		}
		len = 0;
		
	}
	
	return 0;
}
