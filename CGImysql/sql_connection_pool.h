#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include"../log/log.h"

using namespace std;

class connection_pool
{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放数据库连接
	int GetFreeConn();					 //获取空闲的连接数
	void DestroyPool();					 //销毁数据库连接池

	//单例模式
	static connection_pool *GetInstance();

	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);
	
	connection_pool();
	~connection_pool();

private:
	int m_MaxConn;  //最大连接数
    int m_CurConn;  //当前已使用的连接数
    int m_FreeConn; //当前空闲的连接数
	locker lock;
	list<MYSQL *> connList; //连接池
	sem reserve;

private:
	string m_url;			 //主机地址
	string m_Port;		 //数据库端口号
	string m_User;		 //登陆数据库用户名
	string m_PassWord;	 //登陆数据库密码
	string m_DatabaseName; //使用数据库名
	int m_close_log;	//日志开关
};


//通过使用 connectionRAII 对象，可以在对象创建时获取一个数据库连接，
//并在对象销毁时自动释放该连接，避免了手动管理连接资源的繁琐和可能出现的资源泄漏问题。
//这种资源获取和释放的方式常被称为资源获取即初始化（RAII）
class connectionRAII{

public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;			// 数据库连接指针
	connection_pool *poolRAII;		// 连接池对象指针
};

#endif
