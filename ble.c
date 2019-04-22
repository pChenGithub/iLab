/*************************************************************************
	> File Name: ble.c
	> Author: chen.peng
	> Mail: 404285202@qq.com 
	> Created Time: 2019年03月22日 星期五 17时11分47秒
 ************************************************************************/



#include     <unistd.h> 
#include     <errno.h>
#include 	<signal.h>
#include    <pthread.h>
#include	<time.h>

#include "ipc_msg.h"
#include "strTools.h"
#include "uart_port.h"
#include "ble_recv.h"
#include "parse_bleCmd.h"


//#define DEBUG_P 1


struct arg {
	int fd;
	int *msgId;
};

static pthread_t ptid[5];
static int msgId[4];
static char *ble_dev = "/dev/ttymxc1";
static char ble_state = 1;
struct config conf;


static void* uart_bleRead(void *arg) {

	struct arg *p = (struct arg*)arg;
	int fd = p->fd, ret;
	int msgId = (p->msgId)[2];
	char buf[10];
	char cmd;
	struct msg_buf msgBuf;

	while (1) {
		memset(buf, 0, 10);
#if DEBUG_P		
		printf("read data from ble ... \n");
#endif
		if (recv_data(fd, buf, 10) < 0)
			perror("recv data from ble error ... \n");
		cmd = buf[0]&0xf8;
		switch (cmd) {
			case 0x88: //是一个心跳包
				msgBuf.mtype = 200;
				ret = msgsnd(msgId, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
				if(ret==-1)
				{
					printf("[%s] send message err ...\n", __func__);
				}
				break;
			default :
				//printf("others \n");
				msgBuf.mtype = 100;
				memcpy(msgBuf.data, buf, 10);
				ret = msgsnd(msgId, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
				if(ret==-1)
				{
					printf("[%s] send message err ...\n", __func__);
				}
				break;
		}
		
	}
	return 0;
}
static void* do_cmd(void *arg) {

	struct arg *p = (struct arg*)arg;
	int msgId = (p->msgId)[2], ret;
	struct msg_buf msgBuf;
	char *pd = msgBuf.data;

	while (1) {
		ret=msgrcv(msgId, &msgBuf, sizeof(msgBuf.data), 100, 0);
		if(ret==-1)
		{
			printf("[%s] send message err ...\n", __func__);
			continue;
		}
		parse_bleCmd_and_do(pd, 10);
	}

}
static void* pt_heartbeat(void *arg) {

	struct arg *p = (struct arg*)arg;
	int msgIdR = (p->msgId)[2], ret, i;
	int msgIdSf = (p->msgId)[1];
	int msgIdSb = (p->msgId)[3];
	char buf[10];
	char cmd;
	struct msg_buf msgBuf;

	while (1) {
		
		/* 确保超时时间内收到消息，这里要清一次消息队列 */
		while (msgrcv(msgIdR, &msgBuf, sizeof(msgBuf.data), 200, IPC_NOWAIT) != -1)
			;

		memset(buf, 0, 10);
		buf[0] = 0x48;
		msgBuf.mtype = 300;
		memcpy(msgBuf.data, buf, 10);

		ret = msgsnd(msgIdSb, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
		if(ret==-1)
		{
			printf("[%s] send message err ...\n", __func__);
		}
	
		i = 0;
		while (msgrcv(msgIdR, &msgBuf, sizeof(msgBuf.data), 200, IPC_NOWAIT) == -1 && i<50) {
			usleep(100000);
			i++;
		}
		
		if (i<50) {
			
			msgBuf.data[0] = 0xff;
		}else
			msgBuf.data[0] = 0x00;

		msgBuf.mtype = 200;
		ret = msgsnd(msgIdSf, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
		if(ret==-1)
		{
			printf("[%s] send message err ...\n", __func__);
		}

		sleep(20);
		
	}
	
}

int write_cmd_to_file(char *file, char *buf, int len) {

	int fd, n;

	if (!file || !buf || !len) {
	
		perror("check args, error ... \n");
		return -1;
	}

	fd = open(file, O_RDWR|O_CREAT|O_APPEND);
	if (fd<0) {
	
		perror("open save file error ... \n");
		return -1;
	}

	n = write(fd, buf, len);
	if (n!=len) {
	
		perror("write cmd error ... \n");
		return -1;
	}

	close(fd);

	return 0;
}

static void* pt_file_rdwr(void *arg) {

	struct arg *p = (struct arg*)arg;
	int msgIdR = (p->msgId)[1], ret;
	int msgIdS = (p->msgId)[3];
	struct msg_buf msgBuf;
	char *pv = msgBuf.data;
	char curr_state = 1; //1 on,0 off
	char pre_state = 1;
	while (1) {
		ret=msgrcv(msgIdR, &msgBuf, sizeof(msgBuf.data), 0, 0);
		if(ret==-1)
		{
			printf("recv message err n1 \n");
			continue ;
		}

		printf("recv msg type %d ... \n", msgBuf.mtype); 
		switch (msgBuf.mtype) {
			case 100:
				if (ble_state) {
					msgBuf.mtype = 300;
					ret = msgsnd(msgIdS, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
					if(ret==-1)
					{
						printf("[%s] send message err ...\n", __func__);
					}
				} else {
					/* 写文件 */
					printf("ble off line, write to file ... \n");
					write_cmd_to_file("save.txt", msgBuf.data, 10);
				}
				break;
			case 200:
				pre_state = curr_state;
				curr_state = pv[0]?1:0;
				if (pre_state!=curr_state)
					//ble_state = curr_state;
#ifdef TESTDEMO
					printf("test demo ... \n");
					ble_state = 1; //不做在线检测
#else
					printf("release demo ... \n");
					ble_state = curr_state;
#endif

				if (ble_state) {
					msgBuf.mtype = 100;
					ret = msgsnd(msgIdS, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
					if(ret==-1)
					{
						printf("[%s]:%d send message err ...\n", __func__, __LINE__);
					}

					ret=msgrcv(msgIdR, &msgBuf, sizeof(msgBuf.data), 300, 0);
					if(ret==-1)
					{
						printf("[%s]:%d recv message err ...\n", __func__, __LINE__);
					}			
				}
				break;
			default :
				break;
		}
	}

}
static void* pt_sensor_msg(void *arg) {

	struct arg *p = (struct arg*)arg;
	int msgIdR = (p->msgId)[0], ret;
	int msgIdS = (p->msgId)[1];
	char buf[10];
	char cmd;
	struct msg_buf msgBuf;
	char *pv = msgBuf.data; //解析电量计数据的指针
#ifdef TESTDEMO
	buf[2] = 0x00;
	buf[3] = 0x22;
	buf[4] = 0x33;
	buf[5] = 0x44;
	buf[6] = 0x55;
	buf[7] = 0x66;
	buf[8] = 0x77;
	buf[9] = 0x88;
#endif

	while (1) {

#ifndef TESTDEMO
		memset(msgBuf.data, 0, sizeof(msgBuf.data));
		ret=msgrcv(msgIdR, &msgBuf, sizeof(msgBuf.data), -400, 0);
		if(ret==-1)
		{
			printf("recv message err n1 \n");
			continue ;
		}

		/* 在这里封好包 */
		memset(buf, 0, 10);
		switch (msgBuf.mtype) {
			case 200:
#if DEBUG_P
				printf("card read:\n");
				for (i=0;i<5;i++) {
					printf(" 0x%02x ", msgBuf.data[i]);
				}

				printf("\n");
#endif
				buf[0] = 0x40;
				memcpy(buf+2, pv, 5); //暂定唯一号5字节
				break;
			case 300:
#if DEBUG_P
				printf("recvmsg =[%lf]\n",*(double*)(p+2));
#endif
				memcpy(buf+2, pv+2, 8);
				buf[0] = pv[0];
				break;
			default :
				break;
		}
#else
		buf[2]++;
#endif
		msgBuf.mtype = 100;
		memset(msgBuf.data, 0, sizeof(msgBuf.data));
		memcpy(msgBuf.data, buf, 10);

#ifdef TESTDEMO
	msgBuf.data[0] = 0x00; 
	ret = msgsnd(msgIdS, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
		if(ret==-1)
		{
			printf("[%s] send message err ...\n", __func__);
		}

		sleep(1);

msgBuf.data[0] = 0x01; 
	ret = msgsnd(msgIdS, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
		if(ret==-1)
		{
			printf("[%s] send message err ...\n", __func__);
		}

		sleep(1);
msgBuf.data[0] = 0x02; 
	ret = msgsnd(msgIdS, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
		if(ret==-1)
		{
			printf("[%s] send message err ...\n", __func__);
		}
		
		sleep(1);

#else
		ret = msgsnd(msgIdS, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
		if(ret==-1)
		{
			printf("[%s] send message err ...\n", __func__);
		}
#endif



	}
}

static int send_file_save(int fd, char *file, int size) {

	int ffd, n, nt=0, fSize;
	char buf[10];

	if (!file) {
		perror("file is null point ... \n");
		return -1;
	}

	ffd = open("save.txt", O_RDWR);
	if (ffd<0) {
		perror("open save file error ... \n");
		return -1;
	}

	/* 统计文件大小 */
	fSize = lseek(ffd, 0, SEEK_END);
	lseek(ffd, 0, SEEK_SET);
	printf("file size %d ... \n", fSize);

	if (fSize==0)
		return 0;
	
	if (fSize<size)
		size = fSize;

	while (nt<size && (n = read(ffd, buf, 10 )>0)) {
		if (send_data(fd, buf, 10)<0) { //文件发送协议待定
			//printf("2222\n");
			send_data(fd, buf, 10);
		}
		nt += 10;
		usleep(10000);
	}

	ftruncate(ffd, fSize-size); //清空文件
	lseek(ffd, size, SEEK_SET);

	close(ffd);
	return 0;
}

int main(int argc, char *argv[]) {

	int fd, ret, i;
	struct arg pArg;

	init_config(&conf, "config.xml");

	fd = open_port(ble_dev);
	if (fd<0) {
		fprintf(stderr, "%s open error ... \n", ble_dev);
		return -1;
	}
#if 1
	for (i=0;i<4;i++) {
	
		msgId[i] = ipc_msgCreat("/root/ipc_msg.c", 'c'+i);
		if (msgId[i] < 0) {

			fprintf(stderr ,"creat msgId[%d] error ... \n", i);
			return -1;
		}
	}

#else

	msgId[0] = ipc_msgCreat("/root/ipc_msg.c", 'c');
	if (msgId[0] < 0) {

		perror("creat msg id error ... \n");
		return -1;
	}
	msgId[1] = ipc_msgCreat("/root/ipc_msg.c", 'd');
	if (msgId[1] < 0) {

		perror("creat msg id error ... \n");
		return -1;
	}
	msgId[2] = ipc_msgCreat("/root/ipc_msg.c", 'e');
	if (msgId[2] < 0) {

		perror("creat msg id error ... \n");
		return -1;
	}
	msgId[3] = ipc_msgCreat("/root/ipc_msg.c", 'f');
	if (msgId[3] < 0) {

		perror("creat msg id error ... \n");
		return -1;
	}
#endif

	pArg.fd = fd;
	pArg.msgId = msgId;

//线程分离处理
#if 0
	for (i=0;i<5;i++) {
		if (pthread_create (&ptid[4], NULL, uart_bleRead, (void*)&pArg) < 0) {
			perror("pthread uart_bleRead create error ... \n");
			return -1;
		}

	}
#else
	if (pthread_create (&ptid[4], NULL, uart_bleRead, (void*)&pArg) < 0) {
		perror("pthread uart_bleRead create error ... \n");
		return -1;
	}

	if (pthread_create (&ptid[3], NULL, do_cmd, (void*)&pArg) < 0) {
		perror("pthread uart_bleRead create error ... \n");
		return -1;
	}

	if (pthread_create (&ptid[2], NULL, pt_heartbeat, (void*)&pArg) < 0) {
		perror("pthread uart_bleState create error ... \n");
		return -1;
	}

	if (pthread_create (&ptid[1], NULL, pt_file_rdwr, (void*)&pArg) < 0) {
		perror("pthread uart_bleSendFile create error ... \n");
		return -1;
	}

	if (pthread_create (&ptid[0], NULL, pt_sensor_msg, (void*)&pArg) < 0) {
		perror("pthread uart_bleSendFile create error ... \n");
		return -1;
	}
#endif

	int msgId4 = msgId[3];
	int msgId2 = msgId[1];
	char buf[10];
	struct msg_buf msgBuf;

	while (1) {
		memset(msgBuf.data, 0, sizeof(msgBuf.data));
		ret=msgrcv(msgId4, &msgBuf, sizeof(msgBuf.data), 0, 0);
		if(ret==-1)
		{
			printf("recv message err n1 \n");
			continue ;
		}

		switch (msgBuf.mtype) {
			case 300:
				memcpy(buf, msgBuf.data, 10);
				send_data(fd, buf, 10);
				usleep(10000);
			break;
			case 100:
				/* 发文件，待实现 */
				printf("send file ... \n");
				send_file_save(fd, "save.txt", 5120); //这边如果文件太大的话导致不能及时收到其他消息，每次给发5k？1024*5

				msgBuf.mtype = 300;
				ret = msgsnd(msgId2, &msgBuf, sizeof(msgBuf.data), 0);//IPC_NOWAIT
				if(ret==-1)
				{
					printf("[%s] send message err ...\n", __func__);
				}
	
			break;
			default:
			break;
		}
		
	}

	return 0;
}



