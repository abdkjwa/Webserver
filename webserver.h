#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"
#include "./timer/lst_timer.h"
#include"./log/log.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位


class WebServer
{
public:
	WebServer();  // 构造函数
	~WebServer();  // 析构函数

	// 初始化函数，设置服务器参数
	void init(int port, string user, string passWord, string databaseName,
		int log_write, int opt_linger, int trigmode, int sql_num,
		int thread_num, int close_log, int actor_model);

	void thread_pool();  // 创建线程池
	void sql_pool();  // 创建数据库连接池
	void log_write();  // 日志写入函数
	void trig_mode();  // 设置触发模式
	void eventListen();  // 监听事件
	void eventLoop();  // 事件循环
	void timer(int connfd, struct sockaddr_in client_address);  // 定时器函数
	void adjust_timer(util_timer* timer);  // 调整定时器
	void deal_timer(util_timer* timer, int sockfd);  // 处理定时器
	bool dealclinetdata();  // 处理客户端数据
	bool dealwithsignal(bool& timeout, bool& stop_server);  // 处理信号
	void dealwithread(int sockfd);  // 处理读事件
	void dealwithwrite(int sockfd);  // 处理写事件

private:
	//基础配置
	int m_port;		//监听端口
	char* m_root;	//网站根目录
	int m_log_write;	//日志写入标志
	int m_close_log;	//关闭日志标志
	int m_actormodel;	//并发模型

	int m_pipefd[2];	//管道描述符
	int m_epollfd;		//epoll实例描述符
	http_conn* users;	//客户端连接数组

	//数据库相关配置
	connection_pool* m_connPool;	//数据库连接池
	string m_user;			//登录数据库的用户名
	string m_passWord;		//登录数据库的密码
	string m_databaseName;	//使用的数据库名
	int m_sql_num;			//数据库连接池数量

	//线程池相关配置
	threadpool<http_conn>* m_pool;	//线程池
	int m_thread_num;			//线程数目

	//epoll_event相关配置
	epoll_event events[MAX_EVENT_NUMBER];	//epoll事件数组

	int m_listenfd;		//监听套接字描述符
	int m_OPT_LINGER;	//linger option配置
	int m_TRIGMode;		//触发模式
	int m_LISTENTrigmode;	//监听模式
	int m_CONNTrigmode;		//连接模式

	//定时器相关配置
	client_data* users_timer;	//定时器客户数据数组
	Utils utils;		//工具类对象
};

#endif