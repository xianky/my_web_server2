
/*数据库连接池类的实现*/


#include "sql_connection_pool.h"

using namespace std;
connection_pool::connection_pool(){
    this->CurConn = 0;
    this->FreeConn = 0;
}

connection_pool* connection_pool::GetInstance(){
    static connection_pool connPool;
    return &connPool;
}

//构造初始化
//connPool->init("localhost", "root", "root", "qgydb", 3306, 8);
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn){
    this->url = url;
    this->Port = Port;
    this->User = User;
    this->PassWord = PassWord;
    this->DatabaseName = DBName;

    lock.lock();//因为在操作共享资源curconn，freeconn，maxconn
    
    //初始化创建MaxConn个连接放在连接池中
    for(int i = 0; i < MaxConn; i++){
        MYSQL* con = NULL;
        con = mysql_init(con);

        if(con == NULL){
            cout << "Error" << mysql_error(con);
            exit(1);
        }

        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);
        if(con == NULL){
            cout << "Error" << mysql_error(con);
            exit(1);
        }
        connList.push_back(con);
        ++FreeConn;
    }
    reserve = sem(FreeConn);
    this->MaxConn = FreeConn;//////////??????????
    printf("当前连接池中有%d个空闲连接\n", this->FreeConn);
    lock.unlock();
}

//当有请求时，从数据库连接池中返回一个可用的连接，更新使用的和空闲的连接数
MYSQL* connection_pool::GetConnection(){
    MYSQL* con = NULL;
    ////为什么还要有这个判断，就为了不让他阻塞？？？
    if(0 == connList.size()){
        return NULL;
    }
    //因为信号量为0 就返回了
    reserve.wait();
    lock.lock();
    con = connList.front();
    connList.pop_front();

    --FreeConn;
    ++CurConn;
    lock.unlock();
    return con;
}

//释放当前的连接
bool connection_pool::ReleaseConnection(MYSQL* con){
    if(NULL == con)
        return false;

    lock.lock();
    connList.push_back(con);
    ++FreeConn;
    --CurConn;
    lock.unlock();
    reserve.post();
    return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool(){
    lock.lock();
    if(connList.size() > 0){
        list<MYSQL*>::iterator it;
        for(it = connList.begin(); it != connList.end(); it++){
            //MYSQL *con = *it;
            //mysql_close(con);
            mysql_close((*it));
        }
        CurConn = 0;
        FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
    return;
}

//得到当前的空闲的连接数
int connection_pool::GetFreeConn(){
    return this->FreeConn;
}

//析构函数
connection_pool::~connection_pool(){
    this->DestroyPool();
}

connectionRAII::connectionRAII(MYSQL** SQL, connection_pool* connPool){
    *SQL = connPool->GetConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}





