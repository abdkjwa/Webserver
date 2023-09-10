#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include"../log/log.h"
#include"../http/http_conn.h"

class util_timer;

//客户端数据
struct client_data
{
	sockaddr_in address;	//客户端地址
	int sockfd;				//客户端的套接字描述符
	util_timer* timer;		//定时器指针
};

//定时器类
class util_timer
{
public:
	util_timer():prev(NULL),next(NULL){}
public:
	time_t expire;			//定时器的超时时间
	void(*cb_func)(client_data*);	//定时器超时时的回调函数指针，参数为客户端数据结构指针，处理定时器超时事件
	client_data* user_data;	//回调函数的参数，指向客户端数据结构的指针
	util_timer* prev;		//指向前一个定时器的指针
	util_timer* next;		//指向下一个定时器的指针
};

//定时器链表类
class sort_timer_lst
{
public:
	sort_timer_lst();               // 构造函数
	~sort_timer_lst();              // 析构函数

	void add_timer(util_timer* timer);	//添加定时器
	void adjust_timer(util_timer* timer);	//调整定时器
	void del_timer(util_timer* timer);	//删除定时器
	void tick();					//心跳函数，处理超时定时器
private:
	void add_timer(util_timer* timer, util_timer* lst_head);	//在指定链表头部后面添加定时器
	util_timer* head;	//链表头指针，指向第一个定时器（最小超时时间）
	util_timer* tail;	//链表尾指针，指向最后一个定时器（最大超时时间）
};

//工具类
class Utils
{
public:
	Utils() {}             // 构造函数
	~Utils() {}            // 析构函数
	void init(int timeslot);	//初始化函数，设置时间槽的长度

	//对文件描述符设置非阻塞
	int setnonblocking(int fd);

	//将内核事件表注册读事件，采用ET模式，选择开启EPOLLONESHOT
	void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

	//信号处理函数
	static void sig_handler(int sig);

	//设置信号函数
	void addsig(int sig, void(handler)(int), bool restart = true);

	//定时处理任务，重新定时以不断触发SIGALRM函数
	void timer_handler();

	void show_error(int connfd, const char* info);
public:
	static int* u_pipefd;	//静态变量，用于与信号处理函数通信
	sort_timer_lst m_timer_lst;	//定时器链表对象
	static int u_epollfd;	//静态变量，保存epoll描述符
	int m_TIMESLOT;			//时间槽的长度
};

//回调函数
void cb_func(client_data* user_data);
#endif