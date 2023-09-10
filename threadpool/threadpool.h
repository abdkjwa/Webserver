#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool* connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T* request, int state);
    bool append_p(T* request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t* m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T*> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool* m_connPool;  //数据库
    int m_actor_model;          //模型切换
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool* connPool, int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        //循环创建线程，并将工作线程按要求进行运行
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        //将线程进行分离后，不用单独对工作线程进行回收
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T* request, int state)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T* request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void* threadpool<T>::worker(void* arg)
{
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while (true) // 进入无限循环，表示线程池一直在运行
    {
        m_queuestat.wait();     // 等待任务队列非空的条件变量，等待有新的任务到来
        m_queuelocker.lock();   // 对任务队列的互斥锁进行加锁，保证线程安全
        if (m_workqueue.empty())    // 如果任务队列为空
        {
            m_queuelocker.unlock();     // 解锁任务队列的互斥锁
            continue;   // 继续下一次循环，等待新的任务到来
        }
        T* request = m_workqueue.front();   // 从任务队列中获取第一个任务
        m_workqueue.pop_front();    // 移除任务队列中的第一个任务
        m_queuelocker.unlock();      // 解锁任务队列的互斥锁
        if (!request)   // 如果任务为空
            continue;   // 继续下一次循环，处理下一个任务
        if (1 == m_actor_model)     // 如果使用的是并发模型1        
        {
            if (0 == request->m_state)  // 如果任务状态为0，表示需要读取数据
            {
                if (request->read_once())   // 调用读取函数，读取数据是否成功
                {
                    request->improv = 1;     // 数据读取成功标志
                    connectionRAII mysqlcon(&request->mysql, m_connPool);   // 创建数据库连接，保证安全性
                    request->process();     // 处理请求
                }
                else
                {
                    request->improv = 1;    // 数据读取失败标志
                    request->timer_flag = 1;     // 定时器标志
                }
            }
            else    // 如果任务状态不为0，表示需要写入数据
            {
                if (request->write())   // 调用写入函数，写入数据是否成功
                {
                    request->improv = 1;    // 数据写入成功标志
                }
                else
                {
                    request->improv = 1;    // 数据写入失败标志
                    request->timer_flag = 1;    // 定时器标志
                }
            }
        }
        else // 使用的是并发模型0       Proactor模式
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool); // 创建数据库连接，保证安全性
            request->process(); // 处理请求
        }
    }
}
#endif
