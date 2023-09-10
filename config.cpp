#include "config.h"

Config::Config()
{
    //端口号,默认9006
    PORT = 9006;

    //日志写入方式，默认同步
    LOGWrite = 0;

    //触发组合模式,默认listenfd LT + connfd LT
    TRIGMode = 0;

    //listenfd触发模式，默认LT
    LISTENTrigmode = 0;

    //connfd触发模式，默认LT
    CONNTrigmode = 0;

    //优雅关闭链接，默认不使用
    OPT_LINGER = 0;

    //数据库连接池数量,默认8
    sql_num = 8;

    //线程池内的线程数量,默认8
    thread_num = 8;

    //关闭日志,默认不关闭
    close_log = 0;

    //并发模型,默认是proactor
    actor_model = 0;
}

//解析命令行参数
void Config::parse_arg(int argc, char* argv[])
{
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";       //选项列表
    //循环解析命令行参数，从命令行获取选项和参数
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            // 设置端口号
            PORT = atoi(optarg);        //optarg 是 getopt 函数提供的全局变量，你不需要自己定义它
            break;                      //存储使用 getopt 函数获取的选项参数的值
        }   
        case 'l':
        {
            // 设置日志写入方式
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':
        {
            // 设置触发组合模式
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':
        {
            // 设置优雅关闭链接
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':
        {
            // 设置数据库连接池数量
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            // 设置线程池内的线程数量
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            // 设置是否关闭日志
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            // 设置并发模型选择
            actor_model = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}