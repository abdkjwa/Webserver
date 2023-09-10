#include"http_conn.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
const char* doc_root = "/home/william/mywebserver/root";

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;


//从连接池中取出一个数据库连接
void http_conn::initmysql_result(connection_pool* connPool)
{
    //先从连接池中取一个连接
    MYSQL* mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES* result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        m_users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 添加文件描述符到 epoll 实例中，设置监听读事件，使用 ET 模式，选择开启 EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    // 根据 TRIGMode 设置事件类型
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // 对应边缘触发模式（ET）
    else
        event.events = EPOLLIN | EPOLLRDHUP; // 对应水平触发模式（LT）

    // 如果开启了 EPOLLONESHOT，则添加 EPOLLONESHOT 事件
    if (one_shot)
        event.events |= EPOLLONESHOT;

    // 将事件注册到 epoll 实例中
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符为非阻塞模式
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;  // 创建一个epoll_event结构体变量，用于描述事件
    event.data.fd = fd;  // 设置事件关联的文件描述符

    if (1 == TRIGMode)  // 判断是否使用ET触发模式
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;  // 配置事件类型，包括边缘触发模式、一次性事件和关闭连接事件
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;  // 配置事件类型，包括水平触发模式、一次性事件和关闭连接事件

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);  // 修改文件描述符上注册的事件
}

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in& addr, char* root, int TRIGMode,
    int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;  // 待发送的字节数初始化为0
    bytes_have_send = 0;  // 已发送的字节数初始化为0
    m_check_state = CHECK_STATE_REQUESTLINE;  // HTTP请求解析状态初始化为分析请求行状态
    m_linger = false;  // HTTP请求是否保持连接，默认为不保持
    m_method = GET;  // HTTP请求方法初始化为GET
    m_url = 0;  // HTTP请求URL初始化为0
    m_version = 0;  // HTTP请求版本号初始化为0
    m_content_length = 0;  // HTTP请求内容长度初始化为0
    m_host = 0;  // HTTP请求主机名初始化为0
    m_start_line = 0;  // HTTP请求起始行位置初始化为0
    m_checked_idx = 0;  // 当前正在分析的字符在读缓冲区中的位置初始化为0
    m_read_idx = 0;  // 已经从客户端读入的数据的最后一个字节的下一个位置初始化为0
    m_write_idx = 0;  // 待发送的响应报文数据的最后一个字节的下一个位置初始化为0
    cgi = 0;  // 是否启用CGI，默认为不启用
    m_state = 0;  // 记录正在进行的主状态机的状态，初始化为0
    timer_flag = 0;  // 定时器是否超时标志位初始化为0
    improv = 0;  // 主机是否经过优化标志位初始化为0

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);  // 清空读缓冲区
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);  // 清空写缓冲区
    memset(m_real_file, '\0', FILENAME_LEN);  // 清空文件名
}

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buf[m_checked_idx];
        //如果当前字符为'\r'
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;                       
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';     // 将 '\r' 替换为 '\0'
                m_read_buf[m_checked_idx++] = '\0';     // 将 '\n' 替换为 '\0'
                return LINE_OK;                         // 行读取成功
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            // 如果前一个字符是 '\r'，则表示读取到了完整的一行
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0'; // 将 '\r' 替换为 '\0'
                m_read_buf[m_checked_idx++] = '\0'; // 将 '\n' 替换为 '\0'
                return LINE_OK; // 行读取成功
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;   //需要继续读取数据
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE) // 检查读缓冲区是否已满
    {
        return false;
    }
    int bytes_read = 0;

    // LT读取数据
    if (0 == m_TRIGMode) // 如果触发模式为LT模式
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0); // 从套接字中读取数据到读缓冲区
        m_read_idx += bytes_read; // 更新读缓冲区的索引

        if (bytes_read <= 0) // 如果读取失败或对方关闭连接
        {
            return false;
        }

        return true;
    }
    // ET读数据，一次性全部读完
    else
    {
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0); // 从套接字中读取数据到读缓冲区
            if (bytes_read == -1) // 发生错误
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) // 如果套接字暂时不可用，即非阻塞操作被阻塞
                    break; // 跳出循环
                return false;
            }
            else if (bytes_read == 0) // 对方关闭连接
            {
                return false;
            }
            m_read_idx += bytes_read; // 更新读缓冲区的索引
        }
        return true;
    }
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");  // 查找请求行中的第一个空格或制表符
    if (!m_url) {
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if (strcasecmp(method, "GET") == 0) { // 忽略大小写比较
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    }
    else {
        return BAD_REQUEST;
    }
    m_url += strspn(m_url, "\t");   //跳过连续的空格或制表符

    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");      //跳过连续的空格或制表符
    if (strcasecmp(m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;             //支持HTTP/1.1版本
    }
    /**
     * http://192.168.110.129:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');     
    }
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8; // 跳过"https://"前缀
        m_url = strchr(m_url, '/'); 
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    // 当URL为"/"时，默认显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html"); // 将默认的文件名拼接到URL末尾

    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

// 我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
        || ((line_status = parse_line()) == LINE_OK)) {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);

        switch (m_check_state) {
        case CHECK_STATE_REQUESTLINE: {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER: {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST) {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT: {
            ret = parse_content(text);
            if (ret == GET_REQUEST) {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        }
        default: {
            return INTERNAL_ERROR;
        }
        }
    }
    return NO_REQUEST;
}

// 当得到一个完整、正确的HTTP请求时，我们就更新文件，然后分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    // 将 doc_root 路径复制到 m_real_file 中
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);

    const char* p = strrchr(m_url, '/');  // 找到 m_url 中最后一个 '/' 的位置

    //处理CGI
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        //分配内存并复制字符串到m_url_real中
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //提取用户名和密码
        //例：user=John&password=12345
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //如果没有，进行数据增加
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            //没有找到
            if (m_users.find(name) == m_users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                m_users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");          //跳转页面
                else
                {
                    strcpy(m_url, "/registerError.html");   //跳转页面
                    LOG_INFO("注册失败");
                }
                    
            }
            else{
                 strcpy(m_url, "/registerError.html");   //跳转页面
                 LOG_INFO("用户已存在");
            }
               
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1.否则返回0
        else if (*(p + 1) == '2') {
            if (m_users.find(name) != m_users.end() && m_users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    //处理其他路径请求              //注册
    if (*(p + 1) == '0')
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')       //登录
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')       //图片测试
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')       //视频测试
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if(*(p + 1) == '7')        //游戏测试
    {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/game.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 写HTTP响应
bool http_conn::write()
{
    int temp = 0;

    if (bytes_to_send == 0) {
        // 将要发送的字节为0，这一次响应结束。
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }

    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    LOG_INFO("request:%s", m_write_buf);
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len)&&add_content_type()&&add_linger()&&add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        //内部错误，500
        case INTERNAL_ERROR:
        {   
            //状态行
            add_status_line(500, error_500_title);
            //消息报头
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
                return false;
            break;
        }
        //报文语法有误，404
        case BAD_REQUEST:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }
        //资源没有访问权限，403
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }
        //文件存在，200
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            //如果请求的资源存在
            if (m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                //第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                //发送的全部数据为响应报文头部信息和文件大小
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else
            {   //如果请求的资源不存在，则返回空白html文件
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}