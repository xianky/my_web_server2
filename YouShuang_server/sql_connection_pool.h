
/*数据库连接池类头文件
 * 单列模式
 */

#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <cstdio>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <cstring>
#include <iostream>
#include <string>

#include "locker.h"

using namespace std;

class connection_pool{
    public:
        MYSQL* GetConnection();      //获取数据库连接，在连接池中存储的已经连接上数据库的没有使用的连接，连接池中空闲的连接减一
        bool ReleaseConnection(MYSQL* conn);            //  释放连接，把连接放回连接池中，空闲连接加一。
        int GetFreeConn();                      //得到当前的空闲的连接数
        void DestroyPool();                  //销毁所有连接，void mysql_close(MYSQL *mysql)

        //单列模式
        static connection_pool* GetInstance();
        //connPool->init("localhost", "root", "root", "qgydb", 3306, 8);
        void init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn);
        
        connection_pool();
        ~connection_pool();

    private:
        unsigned int MaxConn; //最大连接数
        unsigned int CurConn; //当前已经使用的连接的个数
        unsigned int FreeConn; //当前连接池中空闲的连接数

    private:
        locker lock;           //对连接池中的共享数据进行加锁
        list<MYSQL*> connList; //用来存储连接池中的连接。取出则pop，释放则push，
        sem reserve;        //表示还有多少的空闲连接
    private:
        string url;          //主机地址
        string Port;         //数据库端口号
        string User;         //登陆数据库的用户名
        string PassWord;     //登陆用户的密码
        string DatabaseName;  //使用的数据库的名称
};

class connectionRAII{
    public:
        connectionRAII(MYSQL** con, connection_pool *connPool);
        ~connectionRAII();
    private:
        MYSQL *conRAII;
        connection_pool* poolRAII;
};

#endif
