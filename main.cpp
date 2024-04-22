
#include"log/log.h"
int main(int argc,char* argv[]){
  Log::get_instance()->init("../testLog.txt",0,8192,5000,20);
  LOG_INFO("%d%s",1,"==================");
  LOG_INFO("%d%s",2,"==================");
  LOG_INFO("%d%s",3,"==================");
  LOG_INFO("%d%s",4,"==================");
  return 1;
}
