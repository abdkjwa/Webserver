#include"webserver.h"

WebServer::WebServer()
{
	users = new http_conn[MAX_FD];

	//root文件夹路径
	char server_path[200];
	getcwd(server_path, 200);		//获取当前目录
	char root[6] = "/root";
	m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
	strcpy(m_root, server_path);
	strcat(m_root, root);

	//定时器
	users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
	close(m_epollfd);
	close(m_listenfd);
	close(m_pipefd[1]);
	close(m_pipefd[0]);
	delete[] users;
	delete[] users_timer;
	delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
	int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
	m_port = port;
	m_user = user;
	m_passWord = passWord;
	m_databaseName = databaseName;
	m_log_write = log_write;
	m_OPT_LINGER = opt_linger;
	m_TRIGMode = trigmode;
	m_sql_num = sql_num;
	m_thread_num = thread_num;
	m_close_log = close_log;
	m_actormodel = actor_model;
}

void WebServer::thread_pool()
{
	//线程池
	m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

// 创建数据库连接池
void WebServer::sql_pool()
{
	//初始化数据库连接池
	m_connPool = connection_pool::GetInstance();
	m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);
	//初始化数据库读取表
	users->initmysql_result(m_connPool);
}


//日志写入函数
void WebServer::log_write()
{
	if (m_close_log == 0)
	{
		//初始化日志
		if (m_log_write == 1)
			Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
		else
			Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
	}
}

//设置触发模式
void WebServer::trig_mode()
{
	//LT+LT
	if (m_TRIGMode == 0) {
		m_LISTENTrigmode = 0;
		m_CONNTrigmode = 0;
	}
	//LT + ET
	else if (1 == m_TRIGMode)
	{
		m_LISTENTrigmode = 0;
		m_CONNTrigmode = 1;
	}
	//ET + LT
	else if (2 == m_TRIGMode)
	{
		m_LISTENTrigmode = 1;
		m_CONNTrigmode = 0;
	}
	//ET + ET
	else if (3 == m_TRIGMode)
	{
		m_LISTENTrigmode = 1;
		m_CONNTrigmode = 1;
	}
}

//事件监听
void WebServer::eventListen()
{
	//创建监听套接字
	m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(m_listenfd >= 0);

	//设置优雅关闭连接选项
	if (m_OPT_LINGER == 0)
	{
		struct linger tmp = { 0,1 };
		setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}
	else if (m_OPT_LINGER == 1)
	{
		struct linger tmp = { 1,1 };
		setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}

	int ret = 0;
	struct sockaddr_in address;
	bzero(&address, sizeof(address));

	//设置地址族和端口号
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(m_port);

	int flag = 1;
	//设置地址重用选项
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

	//绑定
	ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
	assert(ret >= 0);

	//监听
	ret = listen(m_listenfd, 5);
	assert(ret >= 0);

	//初始化定时器
	utils.init(TIMESLOT);

	//创建epoll内核事件表
	epoll_event events[MAX_EVENT_NUMBER];
	m_epollfd = epoll_create(5);
	assert(m_epollfd != -1);

	//向epoll内核事件表注册监听套接字事件
	utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
	http_conn::m_epollfd = m_epollfd;

	// 创建用于进程间通信的管道
	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
	assert(ret != -1);
	utils.setnonblocking(m_pipefd[1]);

	// 向 epoll 内核事件表注册管道读取事件
	utils.addfd(m_epollfd, m_pipefd[0], false, 0);

	// 添加信号处理函数
	utils.addsig(SIGPIPE, SIG_IGN);
	utils.addsig(SIGALRM, utils.sig_handler, false);
	utils.addsig(SIGTERM, utils.sig_handler, false);

	// 设置定时器，触发 SIGALRM 信号
	alarm(TIMESLOT);
	
	// 设置工具类的管道和 epoll 描述符
	Utils::u_pipefd = m_pipefd;
	Utils::u_epollfd = m_epollfd;
}


//为用户创建定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
	// 初始化用户连接
	users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

	// 初始化定时器客户数据
	users_timer[connfd].address = client_address;
	users_timer[connfd].sockfd = connfd;

	// 创建定时器，设置回调函数和超时时间，绑定用户数据
	util_timer* timer = new util_timer;
	timer->user_data = &users_timer[connfd];
	timer->cb_func = cb_func;
	time_t cur = time(NULL);
	timer->expire = cur + 3 * TIMESLOT;
	users_timer[connfd].timer = timer;

	// 将定时器添加到定时器链表中
	utils.m_timer_lst.add_timer(timer);


}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer* timer)
{
	time_t cur = time(NULL);	//获取当前时间
	timer->expire = cur + 3 * TIMESLOT;

	// 调整定时器在链表中的位置
	utils.m_timer_lst.adjust_timer(timer);

	LOG_INFO("%s", "adjust timer once");
}

//处理超时的定时器
void WebServer::deal_timer(util_timer* timer, int sockfd)
{
	// 调用定时器的回调函数处理超时事件
	timer->cb_func(&users_timer[sockfd]);

	// 删除定时器
	if (timer)
	{
		utils.m_timer_lst.del_timer(timer);
	}

	// 输出日志，表示关闭对应的套接字文件描述符
	LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

//处理客户端数据
bool WebServer::dealclinetdata()
{
	struct sockaddr_in client_address; // 客户端地址结构体
	socklen_t client_addrlength = sizeof(client_address); // 客户端地址结构体长度

	if (0 == m_LISTENTrigmode)  // 非阻塞模式
	{
		int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength); // ���ܿͻ�������
		if (connfd < 0)
		{
			LOG_ERROR("%s:errno is:%d", "accept error", errno);
			return false;
		}

		if (http_conn::m_user_count >= MAX_FD) // 达到最大连接数限制
		{
			utils.show_error(connfd, "Internal server busy"); // 显示服务器繁忙错误页面
			LOG_ERROR("%s", "Internal server busy");
			return false;
		}

		timer(connfd, client_address);  // 创建并设置定时器
	}
	else
	{
		// 循环接受客户端连接，阻塞模式
		while (1)
		{
			int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
			if (connfd < 0)
			{
				LOG_ERROR("%s:errno is:%d", "accept error", errno);
				break;
			}

			if (http_conn::m_user_count >= MAX_FD)  // 达到最大连接数限制
			{
				utils.show_error(connfd, "Internal server busy"); // 显示服务器繁忙错误页面
				LOG_ERROR("%s", "Internal server busy");
				break;
			}

			timer(connfd, client_address);	// 创建并设置定时器
		}
		return false;
	}
	return true;		// 处理客户端数据成功
}

//处理信号
bool WebServer::dealwithsignal(bool& timeout, bool& stop_server)
{
	int ret = 0;
	int sig;		//信号值
	char signals[1024];		//存储接收到的信号的数组
	ret = recv(m_pipefd[0], signals, sizeof(signals), 0); // 从管道读取信号
	if (ret == -1) {		//发生错误
		return false;
	}
	else if (ret == 0) {	//为收到任何信号
		return false;
	}
	else {					//收到一个或多个信号
		for (int i = 0; i < ret; i++) {
			switch (signals[i]) {
			case SIGALRM:		//定时器信号
				timeout = true;		// 设置超时标志为 true
				break;
			case SIGTERM:		//终止信号
				stop_server = true;		//设置停止服务器标志位true
				break;
			}
		}
	}
	return true;	//处理信号成功
}

//处理读事件
void WebServer::dealwithread(int sockfd)
{
	util_timer* timer = users_timer[sockfd].timer; // 获取当前 sockfd 对应的计时器指针

	//reactor模式
	if (m_actormodel == 1)
	{
		if (timer)			//如果定时器存在
		{
			adjust_timer(timer);	//调整计时器状态
		}

		//若监测到读事件，将该事件放入请求队列
		m_pool->append(users + sockfd, 0);
		while (true)
		{
			//如果尝试了数据读取
			if (users[sockfd].improv)
			{
				if (users[sockfd].timer_flag)		//如果定时器标志为1
				{
					deal_timer(timer, sockfd);  // 处理定时器事件
					users[sockfd].timer_flag = 0; // 重置定时器标志
				}
				users[sockfd].improv = 0; // 重置改进标志
				break; // 结束循环
			}
		}
	}
	else		//proactor模式
	{
		//如果成功读取客户端数据
		if (users[sockfd].read_once()) {
			LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

			//将该事件放入请求队列
			m_pool->append(users + sockfd,0);

			if (timer)
			{
				adjust_timer(timer);	//调整定时器状态
			}
		}
		else	//如果读取客户端数据失败
		{		
			deal_timer(timer, sockfd);		//处理定时器事件
		}
	}
}

//处理写事件
void WebServer::dealwithwrite(int sockfd)
{	
	util_timer* timer = users_timer[sockfd].timer; // 获取该连接的定时器

	//reactor模式
	if (m_actormodel == 1)
	{
		if (timer) {
			adjust_timer(timer);	//调整计时器
		}

		m_pool->append(users + sockfd, 1);		//将该事件加入请求队列
		//循环判断是否所有数据都已写完
		while (true) {
			if (users[sockfd].improv==1)			//如果改进标志为1
			{
				if (users[sockfd].timer_flag == 1) {		// 如果有定时器事件需要处理
					deal_timer(timer, sockfd); // 处理定时器事件
					users[sockfd].timer_flag = 0; // 清除定时器标志
				}
				users[sockfd].improv = 0; // 重置改进标志
				break; // 结束循环
			}
		}
	}
	else		//proactor模式
	{
		if (users[sockfd].write())		//写数据到客户端
		{
			LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr)); // 打印日志，发送数据到客户端

			if (timer) {
				adjust_timer(timer);	//调整定时器状态
			}
		}
		else	//如果写数据到客户端失败
		{
			deal_timer(timer, sockfd);		//处理定时器事件
		}
	}
}

//循环处理客户端数据
void WebServer::eventLoop()
{
	bool timeout = false; // 标记是否发生超时
	bool stop_server = false; // 标记是否停止服务器

	// 主循环，直到服务器停止运行
	while (!stop_server) {		
		int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1); // 使用 epoll 等待事件
		if (number < 0 && errno != EINTR) // epoll_wait 出错，且错误不是被中断
		{
			LOG_ERROR("%s", "epoll failure"); // 打印错误日志
			break;
		}

		//遍历触发的事件
		for (int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd; // 获取事件对应的文件描述符

			//新的客户端连接
			if (sockfd == m_listenfd)
			{
				bool flag = dealclinetdata(); // 处理客户连接数据
				if (false == flag) // 处理失败，则继续下一个事件处理
					continue;
			}
			// 客户端关闭连接或发生错误
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) 
			{
				// 服务器端关闭连接，移除对应的定时器
				util_timer* timer = users_timer[sockfd].timer; // 获取连接对应的定时器
				deal_timer(timer, sockfd); // 处理定时器事件
			}
			// 处理信号
			else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) // 通过 pipe 收到信号
			{
				bool flag = dealwithsignal(timeout, stop_server); // 处理信号
				if (false == flag) // 处理失败，则打印错误日志
					LOG_ERROR("%s", "dealclientdata failure");
			}
			// 处理客户连接上接收到的数据
			else if (events[i].events & EPOLLIN) // 有数据可读
			{
				dealwithread(sockfd); // 处理读事件
			}
			else if (events[i].events & EPOLLOUT) // 有数据可写
			{
				dealwithwrite(sockfd); // 处理写事件
			}
		}

		if (timeout) // 发生超时
		{
			utils.timer_handler(); // 处理定时器事件

			LOG_INFO("%s", "timer tick"); // 打印定时器滴答日志

			timeout = false; // 清除超时标记
		}
	}
}