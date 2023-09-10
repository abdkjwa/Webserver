#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}

// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果设置了max_queue_size，则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        
        // 创建一个阻塞队列来存储日志消息
        m_log_queue = new block_queue<string>(max_queue_size);
        
        pthread_t tid;
        
        // flush_log_thread为回调函数，这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == NULL)
    {
        // 如果没有指定文件路径，则直接使用日期和文件名组成日志文件名
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        // 分离出文件名和路径
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        
        // 使用路径、日期和文件名组成日志文件名
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    // 以追加模式打开日志文件
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};  // 定义一个 timeval 结构体变量 now，用于获取当前时间
    gettimeofday(&now, NULL);  // 获取当前时间并存储在 now 变量中
    time_t t = now.tv_sec;  // 获取当前时间的秒数
    struct tm *sys_tm = localtime(&t);  // 将秒数转换为以年月日时分秒为单位的时间结构体
    struct tm my_tm = *sys_tm;  // 复制时间结构体 sys_tm 的内容到 my_tm 中
    char s[16] = {0};  // 定义一个长度为 16 的字符数组 s，用于存储日志级别字符串
    switch (level)  // 根据传入的日志级别选择相应的字符串
    {
    case 0:
        strcpy(s, "[debug]:");  // 如果日志级别为 0，则将字符串 "[debug]:" 拷贝到数组 s 中
        break;
    case 1:
        strcpy(s, "[info]:");  // 如果日志级别为 1，则将字符串 "[info]:" 拷贝到数组 s 中
        break;
    case 2:
        strcpy(s, "[warn]:");  // 如果日志级别为 2，则将字符串 "[warn]:" 拷贝到数组 s 中
        break;
    case 3:
        strcpy(s, "[erro]:");  // 如果日志级别为 3，则将字符串 "[erro]:" 拷贝到数组 s 中
        break;
    default:
        strcpy(s, "[info]:");  // 如果日志级别不在 0-3 范围内，则将字符串 "[info]:" 拷贝到数组 s 中
        break;
    }
    
    // 写入一个 log，同时对 m_count 进行递增操作，m_split_lines为最大行数限制
    m_mutex.lock();  // 对互斥锁进行加锁，防止多个线程同时写入日志
    m_count++;  // 计数器加一，记录写入的日志数量

    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)  // 判断是否需要创建新的日志文件
    {
        char new_log[256] = {0};  // 定义一个长度为 256 的字符数组 new_log，用于存储新日志文件的路径
        fflush(m_fp);  // 刷新文件流
        fclose(m_fp);  // 关闭当前打开的日志文件
        char tail[16] = {0};  // 定义一个长度为 16 的字符数组 tail，用于存储日志文件名的后缀
        
        // 根据时间构造新的日志文件名后缀
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        
        if (m_today != my_tm.tm_mday)  // 如果当前时间与记录的日期不同，说明已经跨天
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);  // 构造新的日志文件路径
            m_today = my_tm.tm_mday;  // 更新记录的日期
            m_count = 0;  // 重置计数器
        }
        else  // 否则表示还在同一天，但达到了最大行数限制
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);  // 构造新的日志文件路径，加入序号
        }
        m_fp = fopen(new_log, "a");  // 打开新的日志文件以追加方式写入
    }
 
    m_mutex.unlock();  // 对互斥锁进行解锁，允许其他线程进行日志写入操作

    va_list valst;  // 定义一个 va_list 结构体变量 valst，用于处理可变参数列表
    va_start(valst, format);  // 初始化可变参数列表
    
    string log_str;  // 定义一个字符串 log_str，用于存储写入的日志内容
    m_mutex.lock();  // 对互斥锁进行加锁，防止多个线程同时写入日志

    // 根据格式化字符串、可变参数列表和时间信息生成完整的日志内容，并存储在 log_str 中
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);  // 根据格式化字符串和可变参数列表生成日志内容的剩余部分
    m_buf[n + m] = '\n';  // 在日志内容的末尾添加换行符
    m_buf[n + m + 1] = '\0';  // 添加字符串结束符
    log_str = m_buf;  // 将生成的完整日志内容存储到字符串 log_str 中

    m_mutex.unlock();  // 对互斥锁进行解锁，允许其他线程进行日志写入操作

    if (m_is_async && !m_log_queue->full())  // 判断是否开启异步日志写入，并且日志队列未满
    {
        m_log_queue->push(log_str);  // 将日志内容加入日志队列中
    }
    else  // 否则表示同步写入日志
    {
        m_mutex.lock();  // 对互斥锁进行加锁，防止多个线程同时写入日志
        fputs(log_str.c_str(), m_fp);  // 将日志内容写入日志文件
        m_mutex.unlock();  // 对互斥锁进行解锁，允许其他线程进行日志写入操作
    }

    va_end(valst);  // 结束可变参数的获取
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
