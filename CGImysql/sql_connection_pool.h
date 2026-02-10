#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <mysql/mysql.h>
#include <string>
#include <cstring>
#include <mutex>
#include <list>
#include "../log/log.h"

class sql_connection_pool{
    public:
    MYSQL * getConnection();
    bool releaseConnection(MYSQL *conn);
    int getFreeConn();
    void destoryPool();

    static sql_connection_pool * getInstance();
    void init(std::string url, std::string user, std::string passwd, std::string dbname, int port, int close_log, int max_conn);

    private:

    sql_connection_pool();
    ~sql_connection_pool();

    int m_maxConn;
    int m_curConn;
    int m_freeConn;
    std::mutex m_mutex;
    std::list<MYSQL *> connList;
    sem reserve;

    public:
    std::string m_user;
    std::string m_passwd;
    std::string m_dbname;
    std::string m_url;
    int m_port;
    int m_close_log;
};

class connectionRAII{

    public:
    connectionRAII(MYSQL **SQL, sql_connection_pool *conn_pool);
    ~connectionRAII();

    private:
    MYSQL *conRAII;
    sql_connection_pool *poolRAII;
};

#endif