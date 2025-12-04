#include "config.h"

int main(int argc, char *argv[])
{

    std::string user = "rarity";
    std::string passwd = "00000000";
    std::string databasename = "yourdb";

    config config_;
    config_.parse_arg(argc, argv);

    webserver server;

    server.init(config_.port, user, passwd, databasename, 
    config_.logWriteType, config_.optLinger, config_.trigMode,
    config_.sqlNum, config_.threadNum, config_.closeLog,
    config_.actorModel);

    /*
    void init(int port,std::string user,
    std::string passwd,std::string databaseName,
    int log_write,int opt_linger,int trigmode,int sql_num,
    int thread_num,int close_log,int actor_model);
    */

   server.log_write_init();

   server.sql_pool_init();

   server.threadpool_init();

   server.trigger_mode();

   server.eventListen();

   server.eventLoop();

   return 0;
}