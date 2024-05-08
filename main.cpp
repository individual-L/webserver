#include"config.h"
//argument counter , argument vector
int main(int argc, char *argv[]){

    string user = "luo";
    string passwd = "luo";
    string databasename = "dbws";

    Config config;
    config.parse_arg(argc,argv);

    WebServer webserver;

    webserver.init(config.PORT,user,passwd,databasename,config.LOGWriteMode,config.OPT_LINGER,config.TRIGMode,config.sql_num,config.thread_num,config.close_log,config.actor_model);
    //初始化日志
    webserver.log_write();

    //设置一下监听socket和客户端socket的触发模式
    webserver.trig_mode();

    //初始化数据库连接池
    webserver.sql_pool();
    //初始化线程池
    webserver.thread_pool();

    //开始监听
    webserver.eventListen();
    //运行
    webserver.eventLoop();


    return 0;

    
}
