/*********************************************
license:	GNU GENERAL PUBLIC LICENSE  Version 3, 29 June 2007
author:		arvik
email:		1216601195@qq.com
blog:		http://blog.csdn.net/u012819339
github:		https://github.com/arviklinux/impush
date:		2017.03.07
*********************************************/

#include "imclib.h"

//static imclib_t imc;
//static immsglist_t *pmsghead = NULL;
static int16_t s_id=0;

//static imcb imspushcb = NULL;


/*
根据网卡名称获取其mac地址，缓冲区mac必须大于6个字节
*/
int GetLocalDevMac0X(char *devname, uint8_t *mac)
{
	struct ifreq ifr;
	int fd;

	memset(&ifr, 0, sizeof(ifr));
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd == -1)
		return -1;
	
	strcpy(ifr.ifr_name, devname);
	if(ioctl(fd, SIOCGIFHWADDR, &ifr) < 0)
	{
		close(fd);
		return -2;
	}

	memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
	close(fd);

	return 0;
}

void setnonblocking(int sock)
{
        int opts;
        opts = fcntl(sock, F_GETFL);

        if(opts < 0) {
                perror("fcntl(sock, GETFL)");
                exit(1);
        }

        opts = opts | O_NONBLOCK;

        if(fcntl(sock, F_SETFL, opts) < 0) {
                perror("fcntl(sock, SETFL, opts)");
                exit(1);
        }
}

int nob_write(int fd, char *buf, size_t len, int timeout)
{
	int ret=-1;
	int nsend = 0;
	int i = timeout * 5;  //200ms重试一次

	while(nsend < len)
	{
		ret = write(fd, buf, len - nsend);
		if(ret > 0)
		{
			nsend += ret;
		}
		if(ret == 0) //套接字关闭
		{
			break;
		}
		else
		{
			if(EINTR == errno)//连接正常，操作被中断，可继续发送
				continue;
			else if(EWOULDBLOCK == errno || EAGAIN== errno)//连接正常，但发送缓冲区没有空间，等待下一次发
			{
				if(timeout-- < 0)
					usleep(200000);
					continue;
			}
			else  //出错
				break;
		}
	}

	return ret;
}

int nob_read(int fd, char *buf, size_t len, int timeout)
{
	int ret = -1;
	int i = timeout * 5;  //200ms重试一次

	do
	{
		ret = read(fd, buf, len);
		if(ret >= 0) //ret=0表示套接字关闭，读到fin
			return ret;
		else if(ret < 0)
		{
			if(EINTR == errno || EWOULDBLOCK == errno || EAGAIN== errno) 
			{
				ret = -2; //超时
				//sleep(1);
				usleep(200000); 
			}
			else 
				ret = -1;
		}
	}while(timeout--);

	return ret;
}

int setsocktimeout(int sock, uint32_t timeout) //适用于阻塞套接字
{
	struct timeval t1;

	t1.tv_sec = timeout;
	t1.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&t1, sizeof(t1));

	return 0;
}


static inline void c_imdefault_header(impush_header_t *oh)
{
	oh->ver = CURRENT_VER;
	//oh->type = 0; 
	oh->warn = 0; 
	oh->reserve = 0; 
	oh->len = htons(0); 
	oh->session_id = htons(s_id++);
}


/*
注册信息：
fd: 非阻塞套接字
info：impush协议注册用的信息
n_getid: 服务器返回的id存放位置
rtt: 读写数据超时时间
*/
int c_imsign(int fd, uint8_t *info, uint32_t *n_getid, uint32_t rtt)
{
	uint8_t buf[64];
	impush_header_t *im = (impush_header_t *)buf;
	uint8_t *p = &buf[8];
	int ret=0;

	c_imdefault_header(im);
	im->type = IMPUSH_SIGN;
	im->len = htons(8);
	memcpy(p, info, 8);
	//write(fd, buf, 16);
	ret = nob_write(fd, buf, 16, rtt);
	if(ret <= 0)
		return -1;

	memset(buf, 0, sizeof(buf));
	ret = nob_read(fd, buf, 12, rtt); //12字节
	if(ret <= 0)
		return -2;

	if(ntohs(im->len) != 4 )
		return -3; //解析版本不对，1.0版本消息内容必须为4字节uint32_t的id

	if(im->warn == 0)
	{
		*n_getid = ntohl(*(uint32_t *)p);
		printf("sign success! get id:%u\n", *n_getid);
		ret = 0;
	}
	else
	{
		ret = -1;
		printf("sign error, warn:%d\n", im->warn);
	}

	return ret;
}

/*
登录服务器
fd: 非阻塞套接字
info：impush协议注册用的信息
n_getid: 服务器返回的id存放位置
rtt: 读写数据超时时间
*/

int c_imlogin(int fd, uint32_t n_id, uint32_t rtt)
{
	uint8_t buf[24];
	impush_header_t *im = (impush_header_t *)buf;
	uint8_t *p = &buf[8];
	int ret=0;

	c_imdefault_header(im);
	im->type = IMPUSH_LOGIN;
	im->len = htons(4);
	
	*(uint32_t *)p = htonl(n_id);

	ret = nob_write(fd, buf, 12, rtt);
	if(ret <= 0)
		return -1;

	memset(buf, 0, sizeof(buf));
	ret = nob_read(fd, buf, 8, rtt); //12字节
	if(ret <= 0)
		return -2;

	if(ntohs(im->len) != 0)
		return -3;
	
	if(im->warn == 0)
	{
		printf("login success!\n");
		ret = 0;
	}
	else
	{
		ret = -1;
		printf("login error, warn:%d\n", im->warn);
	}

	return ret;
}

int c_imalive(int fd, uint32_t rtt)
{
	uint8_t buf[64];
	impush_header_t *im = (impush_header_t *)buf;
	uint8_t *p = &buf[8];
	int ret=0;

	printf("client alive...\n");
	c_imdefault_header(im);
	im->type = IMPUSH_ALIVE;

	//write(fd, buf, 8);
	ret = nob_write(fd, buf, 8, rtt);
	if(ret <= 0)
		return -1;

	memset(buf, 0, sizeof(buf));
	//read(fd, buf, sizeof(buf)); //8个
	ret = nob_read(fd, buf, 8, rtt); //12字节
	if(ret <= 0)
		return -2;

	if(ntohs(im->len) != 0)
		return -3;

	if(im->warn == 0)
	{
		printf("alive success!\n");
		ret = 0;
	}
	else
	{
		ret = -4;
		printf("alive error, warn:%d\n", im->warn);
	}

	return ret;
}



//int im_connect(int8_t *ipstr, uint16_t port, int8_t *info, imclib_t *imc)
int im_connect(imclib_t *imc)
{
	int cfd;
	struct sockaddr_in serveraddr;
	uint32_t rtt = imc->rtt;
	int ret=0;

	//memcpy(imc->info, info, 8);
	cfd = socket(AF_INET, SOCK_STREAM, 0); //异步socket
	if(cfd == -1)
		return -1;

	imc->imfd = cfd;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = imc->n_sip; //inet_addr(ipstr);//htonl(INADDR_ANY); //inet_addr("42.96.130.249");
	serveraddr.sin_port = htons(imc->port);

	if(connect(cfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr) ) < 0)
	{
		printf("connect server failed, errno:%d\n", errno);
		ret = -2;
		goto exit;
	}

	setnonblocking(cfd);
	if(c_imsign(cfd, imc->info, &(imc->id), rtt) != 0)
	{
		ret = -3;
		goto exit;
	}
	
	if(c_imlogin(cfd, imc->id, rtt) != 0 )
	{
		ret = -4;
		goto exit;
	}

/*
	if(c_imalive(cfd, rtt) != 0)
	{
		ret = -5;
		goto exit;
	}
*/
	return 0;
exit:
	close(imc->imfd);
	return ret;
}

int im_addmsg(int8_t *buf, int16_t len, int8_t method, imclib_t *imc)
{
	immsglist_t *p = imc->msglist, *p1;
	int8_t *b;
	impush_header_t *im;
	static uint16_t s_id;
	int size=0;

	
	if(len > imc->maxlen)
		return -1;

	p = (immsglist_t *)malloc(sizeof(immsglist_t));
	if(p == NULL)
		return -2;
	
	b = malloc(len + sizeof(impush_header_t) );
	if(b == NULL)
	{
		free(p);
		return -3;
	}

	im = (impush_header_t *)b;
	memcpy(b + sizeof(impush_header_t), buf, len);
	im->ver = CURRENT_VER;
	im->type = IMPUSH_CPUSH; 
	im->warn = method; 
	im->reserve = 0; 
	im->len = htons(len); 
	im->session_id = htons(s_id++);
	p->msg = b;
	p->len = len + sizeof(impush_header_t);
	p->next = NULL;

	if(imc->msglist == NULL)
	{
		imc->msglist = p;
		return 0;
	}
	p1 = imc->msglist;
	while(p1->next != NULL)
	{
		p1 = p1->next;
		size++;
	}

	if(size > imc->msglen)
		return -5;

	p1->next = p;
	
	return 0;
}

int register_spush_cb(imcb cb, int force, imclib_t *imc)
{
	if(cb == NULL)
		return -1;

	if(force == 1)
	{
		imc->cb = cb;
		return 0;
	}

	if(imc->cb == NULL)
	{
		imc->cb = cb;
		return 0;
	}
	
	return -1;
}


int im_readcall(imclib_t *imc)
{
	static int8_t buf[2000];
	impush_header_t *im = (impush_header_t *)buf;
	int8_t *msg = buf + 8;
	int ret=-1;
	int16_t msglen;
	imcb imspushcb = imc->cb;

	while(1) //有消息则一直处理
	{
		memset(buf, 0, 8);
		ret = nob_read(imc->imfd, buf, 8, imc->rtt);
		if(ret <= 0)
			return ret;

		if(im->ver != CURRENT_VER)
			return -2;

		msglen = ntohs(im->len);
		if(msglen != 0)
		{
			if(nob_read(imc->imfd, msg, msglen, 1 ) <= 0)
				return ret;
		}

		imc->lasttime = time(NULL);
		switch(im->type)
		{
			case IMPUSH_SPUSH:
				if(imspushcb != NULL)
					imspushcb(msg, msglen);

			break;
			case IMPUSH_ALIVE:  //处理服务器回复的保活包，一般来说记录时间，
			break;
			default:
				break;
		}
	}

	return 1;
}


int im_aliveloop(imclib_t *imc)
{
	int ret = 0;
	immsglist_t *p = imc->msglist, *p1;
	time_t currenttime, tmptime;
	uint8_t buf[1024];
	impush_header_t *im = (impush_header_t *)buf;
	int imloopcont=0;
	int continue_val = imc->imloopcontinue;
	//int timesecond=0;

	c_imdefault_header(im);
	im->type = 9;

	tmptime = currenttime = time(NULL);
	do
	{
		//每秒周期执行用户自定义函数
		//if(timesecond++ >= 2) 
		//{
			//timesecond = 0;
			if(imc->imloop != NULL && imloopcont++ >= imc->imloopcount)
			{
				imloopcont = 0;
				if(continue_val>0)
				{
					continue_val--;
					imc->imloop(imc);
				}
				else if(continue_val<0)
					imc->imloop(imc);
			}
		//}

		//每次驱动都检查有无消息需要上传
		p = imc->msglist;
		while(p != NULL)
		{
			ret = nob_write(imc->imfd, p->msg, p->len, imc->rtt);
			if(ret <= 0)
				break;
			p = p->next;

		}

		p = imc->msglist;
		while( p != NULL) // free
		{
			p1 = p;
			free(p->msg);
			free(p);
			p = p1->next;
		}
		imc->msglist = NULL;

		//心跳机制，距离最近一次服务器向客户端 发送/回复 任何消息的时长超过heartbeat时长则发送心跳包
		currenttime = time(NULL);

		if(currenttime - imc->lasttime > imc->rto)  //重置链接
		{
			printf("timeout ! close socket and try connect server...\n");
			close(imc->imfd);
			printf("connect server...\n");
			while(im_connect(imc) != 0 ) //成功后至少要一次心跳
			{
				sleep(2);
				//printf("connect server...\n");
			}
			imc->lasttime = time(NULL);
		}

		if(currenttime - imc->lasttime > imc->heartbeat)
		{
			im->session_id = s_id++;
			nob_write(imc->imfd, buf, 8, imc->rtt);	
		}

		im_readcall(imc); // 1s驱动一次
		//usleep(500000); //500ms驱动一次
	}
	while(imc->run);

	//exit
	return 0;
}


void init_imc(imclib_t *imc)
{
	memset(imc, 0, sizeof(imclib_t));
	imc->heartbeat = 30;
	imc->port = 5200;
	imc->maxlen = 1000;
	imc->rtt = 1;
	imc->cb = NULL;
	imc->msglist = NULL;
	imc->msglen = 10;
	imc->run = 1;
	imc->imloop = NULL;
	imc->imloopcount = 10;
	imc->imloopcontinue = 1;
	imc->lasttime = time(NULL);
	imc->rto = imc->heartbeat + 15;
}

