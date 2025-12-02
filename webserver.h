#ifndef WEBSERVER_H
#define WEBSERVER_H


#include <unistd.h>
#include <cstdlib>
#include <string>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./httpconn/httpconn.h"


constexpr int MAX_FD = 65536;
constexpr int TIMESLOT = 5;
constexpr int MAX_EVENT_NUMBER = 10000;

class webserver {
    public:
    webserver();
    ~webserver();

    void init(int port,std::string user,std::string passwd,std::string databaseName,int log_write,int opt_linger,int trigmode,int sql_num,int thread_num,int close_log,int actor_model);

    void threadpool_init ();
    void sql_pool_init ();
    void log_write_init ();
    void eventListen ();
    void eventLoop ();
    void timer (int connfd,struct sockaddr_in client_address);
    void adjust_timer (util_timer *timer,int sockfd);
    bool deal_timer (util_timer *timer,int sockfd);
    bool dealclientdata ();
    bool dealwithsignal (bool& timeout,bool& stop_server);
    void dealwithread (int sockfd);
    void dealwithwrite (int sockfd);


    public:
    int m_port;  // 端口号
    char *m_root;
    int m_closeLog;
    int m_logWriteType;
    int m_actorModel;

    int m_pipefd[2];
    int m_epollfd;
    httpConn *users;


    //数据库相关
    sql_connection_pool *m_connPool;
    std::string m_user;
    std::string m_passwd;
    std::string m_databaseName;
    int m_sqlNum;

    //线程池相关
    threadpool<httpConn> *m_threadpool;
    int m_threadNum;

    //epoll相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;  // 监听socket
    int m_optLinger; // 优雅关闭
    int m_trigMode;  // 触发模式
    int m_listenTrigmode; // 监听触发模式
    int m_connectTrigmode; // 连接触发模式

    //定时器相关
    client_data *users_timer;
    Utils utils;
 };


 #endif