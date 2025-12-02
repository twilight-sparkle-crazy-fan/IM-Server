#include "config.h"

config::config(){

    port = 9006;
    logWriteType = 0;  // 同步写日志
    trigMode = 0;  //默认LT + LT
    connectTrigmode = 0;//connfd触发模式 默认LT
    listenTrigmode = 0;//listenfd触发模式 默认LT

    optLinger = 0; // 默认不使用linger
    sqlNum = 8;
    threadNum = 8;
    closeLog = 0;  //默认不关闭日志
    actorModel = 0; //默认是proactor模式
}

void config::parse_arg(int argc, char *argv[]) {
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
            case 'p':
            {
                port = atoi(optarg);
                break;
            }
            case 'l':
            {
                logWriteType = atoi(optarg);
                break;
            }
            case 'm':{
                trigMode = atoi(optarg);
                break;
            }
            case 'o':{
                optLinger = atoi(optarg);
                break;
            }
            case 's':{
                sqlNum = atoi(optarg);
                break;
            }
            case 't':{
                threadNum = atoi(optarg);
                break;
            }
            case 'c':{
                closeLog = atoi(optarg);
                break;
            }
            case 'a':{
                actorModel = atoi(optarg);
                break;
            }
            default:
                break;
        }
     }
 }