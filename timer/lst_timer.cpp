#include"lst_timer.h"

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;


sort_timer_lst::sort_timer_lst()
{
	head = NULL;
	tail = NULL;
}

sort_timer_lst::~sort_timer_lst()
{
	util_timer* tmp = head;
	while (tmp) {
		head = head->next;
		delete tmp;
		tmp = head;
	}
}

//添加定时器
void sort_timer_lst::add_timer(util_timer* timer)
{
	if (!timer)
		return;
	if (!head) {
		head = tail = timer;
		return;
	}
	if (timer->expire < head->expire) {
		timer->next = head;
		head->prev = timer;
		head = timer;
		return;
	}
	add_timer(timer, head);
}


//在指定链表头部后面添加定时器
void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head)
{
	util_timer* prev = lst_head;
	util_timer* tmp = prev->next;
	while (tmp) {
		if (timer->expire < tmp->expire) {
			prev->next = timer;
			timer->prev = prev;
			timer->next = tmp;
			tmp->prev = timer;
			break;
		}
		prev = tmp;
		tmp = tmp->next;
	}
	if (!tmp) {
		prev->next = timer;
		timer->prev = prev;
		timer->next = NULL;
		tail = timer;
	}
}

//调整定时器位置，保证链表按照expire时间的递增顺序排列
void sort_timer_lst::adjust_timer(util_timer* timer)
{
	if (!timer) {
		return;
	}
	util_timer* tmp = timer->next;
	if (!tmp || (timer->expire < tmp->expire)) {
		return;
	}
	if (timer == head) {
		head = head->next;
		head->prev = NULL;
		timer->next = NULL;
		add_timer(timer, head);
	}
	else
	{
		timer->prev->next = tmp;
		tmp->prev = timer->prev;
		add_timer(timer, tmp);
	}
}

//从链表中删除定时器
void sort_timer_lst::del_timer(util_timer* timer)
{
	if (!timer)
		return;
	if (timer == head && timer == tail) {
		head = NULL;
		tail = NULL;
		delete timer;
		return;
	}
	if (timer == head) {
		head = head->next;
		head->prev = NULL;
		delete timer;
		return;
	}
	else if (timer == tail) {
		tail = tail->prev;
		tail->next = NULL;
		delete timer;
		return;
	}
	else {
		timer->prev->next = timer->next;
		timer->next->prev = timer->prev;
		delete timer;
	}
}

//处理定时器链表中超时的定时器
void sort_timer_lst::tick()
{
	if (!head)			// 如果链表为空，则直接返回
		return;
	time_t cur = time(NULL);	//获取当前时间
	util_timer* tmp = head;		//从头结点开始遍历链表
	while (tmp) {
		if (cur < tmp->expire)
			break;
		tmp->cb_func(tmp->user_data);	//调用定时器的回调函数，执行相应的操作
		head = tmp->next;
		if (head)
			head->prev = NULL;
		delete tmp;
		tmp = head;
	}
}

//初始化函数，设置时间槽的长度
void Utils::init(int timeslot)
{
	m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
	epoll_event event;
	event.data.fd = fd;

	if (1 == TRIGMode)
		event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	else
		event.events = EPOLLIN | EPOLLRDHUP;

	if (one_shot)
		event.events |= EPOLLONESHOT;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
	//为保证函数的可重入性，保留原来的errno
	int save_errno = errno;
	int msg = sig;
	send(u_pipefd[1], (char*)&msg, 1, 0);		//管道将信号值发送出去
	errno = save_errno;		//恢复原来的errno值

}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));	//清空sa结构体
	sa.sa_handler = handler;		//设置信号处理函数
	if (restart)
		sa.sa_flags |= SA_RESTART;	//如果restart为真，则设置SA_RESTART标志，用于自动重启被信号中断的系统调用
	sigfillset(&sa.sa_mask);		//将所有信号添加到sa_mask集合中，以阻塞在信号处理函数执行期间可能发生的其他信号
	assert(sigaction(sig, &sa, NULL) != -1);	//注册信号处理函数，将sa结构体中的属性应用到指定信号sig上
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
	m_timer_lst.tick();
	alarm(m_TIMESLOT);
}

// 将错误信息发送给客户端，并关闭连接
void Utils::show_error(int connfd, const char* info)
{
	send(connfd, info, strlen(info), 0);  // 将错误信息通过连接套接字发送给客户端
	close(connfd);   // 关闭连接套接字
}

// 回调函数，用于处理客户端连接断开事件
void cb_func(client_data* user_data)
{
	epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);   // 从 epoll 实例中删除对应的文件描述符
	assert(user_data);  // 断言，确保 user_data 不为空

	close(user_data->sockfd);  // 关闭与客户端的连接套接字
	http_conn::m_user_count--;  // 客户端连接数减一
}