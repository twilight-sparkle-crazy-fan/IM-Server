#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

class config {

    public :
    config();
    ~config();

    void parse_arg(int argc, char *argv[]);

    int port;

    int logWriteType;

    int trigMode;

    int listenTrigmode;

    int connectTrigmode;

    int optLinger;

    int sqlNum;

    int threadNum;

    int closeLog;

    int actorModel;
};

#endif