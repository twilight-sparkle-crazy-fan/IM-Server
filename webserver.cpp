#include "webserver.h"

webserver::webserver()
{
    users = std::make_unique<httpConn[]>(MAX_FD);

    char buffer[256];
    if (getcwd(buffer, sizeof(buffer)) != nullptr)
    {
        m_root = std::string(buffer) + "/root";
    }
    else
    {
        m_root = "/root";
    }

    m_timer = std::make_unique<client_data[]>(MAX_FD);
}

webserver::~webserver()
{

    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
}

void webserver::init(int port, std::string user, std::string passwd, std::string databaseName, int log_write, int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passwd = passwd;
    m_sqlNum = sql_num;
    m_databaseName = databaseName;
    m_threadNum = thread_num;
    m_logWriteType = log_write;
    m_closeLog = close_log;
    m_actorModel = actor_model;
    m_trigMode = trigmode;
    m_optLinger = opt_linger;
}

void webserver::trigger_mode()
{
    // LT LT
    if (0 == m_trigMode)
    {
        m_listenTrigmode = 0;
        m_connectTrigmode = 0;
    }
    // LT ET
    else if (1 == m_trigMode)
    {
        m_listenTrigmode = 0;
        m_connectTrigmode = 1;
    }
    // ET LT
    else if (2 == m_trigMode)
    {
        m_listenTrigmode = 1;
        m_connectTrigmode = 0;
    }
    // ET ET
    else if (3 == m_trigMode)
    {
        m_listenTrigmode = 1;
        m_connectTrigmode = 1;
    }
}

void webserver::log_write_init()
{
    if (0 == m_closeLog)
    {
        if (1 == m_logWriteType)
        {
            Log::get_instance().init("./ServerLog", m_closeLog, m_logWriteType, 8000000);
        }
        else
        {
            Log::get_instance().init("./ServerLog", m_closeLog, m_logWriteType, 8000000);
        }
    }
}

void webserver::sql_pool_init()
{
    m_connPool = sql_connection_pool::getInstance();
    m_connPool->init("localhost", m_user, m_passwd, m_databaseName, 3306, m_closeLog, m_sqlNum);
    // void init(std::string url, std::string user, std::string passwd, std::string dbname, int port, int close_log, int max_conn);

    users[0].initmysql_result(m_connPool);
}

void webserver::threadpool_init()
{
    m_threadpool = std::make_unique<threadpool<httpConn>>(m_actorModel, m_connPool, m_threadNum);
}

void webserver::eventListen()
{
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    // 协议族  套接字类型  协议号
    assert(m_listenfd >= 0);

    if (m_optLinger == 0)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (m_optLinger == 1)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
        // SOL_SOCKET 表示这是通用套接字层面的选项   SO_LINGER 表示延迟关闭
    }

    /*
    struct sockaddr_in {
    short            sin_family;   // 协议族 (AF_INET)
    unsigned short   sin_port;     // 端口号 (网络字节序)
    struct in_addr   sin_addr;     // IP地址
    char             sin_zero[8];  // 填充字节 (为了和通用sockaddr对齐)
    };
    */

   int ret = 0;
   struct sockaddr_in address;
   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;  // IPV4
   address.sin_addr.s_addr = htonl(INADDR_ANY);  //htonl 主机字节序 -> 网络字节序  INADDR_ANY 表示本机所有IP地址
   address.sin_port = htons(m_port);  // htons 主机字节序 -> 网络字节序

   int flag = 1;
   setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)); // 设置端口复用
   ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));  //把地址结构绑定到监听套接字上
   assert(ret >= 0);
   ret = listen(m_listenfd, 5);  // 将监听套接字变为可连接状态，暂存5个连接请求
   assert(ret >= 0);

   utils.init(TIMESLOT);

   //epoll 创建内核时间表
   //epoll_event events[MAX_EVENT_NUMBER];   疑似作者写错了,这个是成员变量
   m_epollfd = epoll_create(5);
   assert(m_epollfd != -1);
   
   utils.addfd(m_epollfd, m_listenfd, false, m_listenTrigmode);
   httpConn::m_epollfd = m_epollfd;

   ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
   // PF_UNIX 本地协议族  SOCK_STREAM 流式传输  
   assert(ret != -1);
   utils.setnonblocking(m_pipefd[1]);
   // 写端非阻塞
   utils.addfd(m_epollfd, m_pipefd[0], false, 0);
   // 把读段注册到epoll

   utils.addsig(SIGPIPE, SIG_IGN);   // 忽略SIGPIPE信号  SIG_IGN  忽略信号
   utils.addsig(SIGALRM, utils.sig_handler, false);  // 为SIGALRM信号设置自定义处理函数
   utils.addsig(SIGTERM, utils.sig_handler, false);  // 为SIGTERM信号设置自定义处理函数

   alarm(TIMESLOT);

   Utils::u_pipefd = m_pipefd;
   Utils::m_epollfd = m_epollfd;

}