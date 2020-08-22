
/*http_conn.h
 * 主要是一个任务类的定义，http_conn类，其中包含这个处理任务的方法process();
 * 用于web server中
 */

#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/uio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>
//使用可变参数的函数
#include <cstdarg> //变量类型va_list这是一个适用于 va_start()、va_arg() 和 va_end() 这三个宏存储信息的类型。
#include "threadpool.h"
#include "sql_connection_pool.h"
class http_conn
{
public:
    /*文件名的最大长度*/
    static const int FILENAME_LEN = 200;
    /*读缓冲区的大小*/
    static const int READ_BUFFER_SIZE = 2048;
    /*写缓冲区的大小*/
    static const int WRITE_BUFFER_SIZE = 4096;
    /*dir文件的html文件缓冲区*/
    static const int DIR_HTML_BUFFER_SIZE = 3072;
    /*HTTP请求方法， 但我们只支持GET*/
    //enum 枚举类型 
    enum METHOD{GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};
    /*解析客户请求时，主状态机所处的状态*/
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    /*服务器端处理HTTP请求的可能结果*/
    enum HTTP_CODE{NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, DIR_REQUEST,
                    INTERNAL_ERROR, CLOSE_CONNECTION};
    /*行的读取状态*/
    enum LINE_STATUS{LINE_OK = 0, LINE_BAD, LINE_OPEN};

public:
    http_conn() {}
    ~http_conn() {}

public:
    /*初始化新接收的连接*/
    void init(int socket, const sockaddr_in &addr);
    /*关闭连接*/
    void close_conn(bool real_close = true);
    /*处理客户请求*///在线程入口函数中去调用每个任务的这个处理函数
    void process();
    /*非阻塞读操作*/
    bool read();
    /*非阻塞写操作*/
    bool write();
    //m_address是private
    struct sockaddr_in* get_address(){
        return &m_address;
    }
    //使用RALL机制，从连接池中选择一个连接，
    //去连接数据库，获取数据库的全部信息，initmysql_result函数结束就释放这个对象，也就将连接放回连接之中了
    void initmysql_result(connection_pool *connPool);

private:
    /*初始化连接*/
    void init();//和上面的那个公有方法不一样
    /*解析HTTP请求*/
    HTTP_CODE process_read();
    /*填充HTTP应答*/
    bool process_write(HTTP_CODE ret);//根据解析HTTP请求的结果来填充应答信息

    /*下面的这一组函数被process_read函数调用以分析HTTP请求*/
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    //当前正在读缓冲区中解析的行
    char* get_line() {return m_read_buf + m_start_line;}; //???????????
    LINE_STATUS parse_line();

    /*下面这一组函数被process_write调用以填充HTTP应答*/
    void unmap();
    void write_dir_html(const char* dirname);
    bool add_response(const char* format, ...);//可变参数函数，需要头文件#include<cstdarg>
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();//?????????????
    bool add_blank_line();

public:
    /*所有的socket上的事件都被注册到同一个epoll内核事件表中，所以将epoll文件描述符设置为静态的*/
    static int m_epollfd;
    /*统计用户数量*/
    static int m_user_count;
    MYSQL *mysql;
    
/*每一个任务不一样的数据成员, 上面的每一个方法都是公用的任务类的方法*/
private:
    /*该HTTP连接的 cfd和对方的socket地址*/
    int m_sockfd;
    sockaddr_in m_address;

    /*读缓冲区*/
    char m_read_buf[READ_BUFFER_SIZE];
    /*标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置*/
    int m_read_idx;
    /*当前正在分析的字符在读缓冲区中位置*/
    int m_checked_idx;
    /*当前正在解析的行的起始位置*/
    int m_start_line;
    /*写缓冲区*/
    char m_write_buf[WRITE_BUFFER_SIZE];
    /*写缓冲区中待发送的字节数*/
    int m_write_idx;

    /*主状态机当前所处的状态*/
    CHECK_STATE m_check_state;
    /*请求方法*/
    METHOD m_method;

    /*客户机请求的目标文件的完整路径，其内容等于doc_root + m_rul, doc_root是网站的根目录*/
    char m_real_file[FILENAME_LEN];
    /*客户机请求的目标文件的文件名*/
    char *m_url;
    /*HTTP协议版本号，我们仅支持HTTP/1.1*/
    char *m_version;
    /*主机名*/
    char *m_host;
    /*HTTP请求的消息体长度*/
    int m_content_length;
    /*HTTP请求是否要求保持连接*/
    bool m_linger;

    /*客户请求的目标文件被mmap到内存中的起始位置*/
    char* m_file_address;
    /*目标文件的状态，通过它我们可以判断文件是否存在、是否为目录、是否可读、并获取文件大小等信息*/
    struct stat m_file_stat;

    //写缓冲区
    int bytes_to_send;
    int bytes_have_send;

    /*我们将采用writev来执行写操作，所以定义下面的两个成员，其中m_iv_count表示被写的内存块的数量*/
    //readv和writev是对不连续内存区域的一次性读写，避免了多次使用read和write去读写不连续的内存区域。
    struct iovec m_iv[2];
    int m_iv_count;

    int cgi;  //是否启用POST
    char* m_string;  //存储请求头数据
    
    //是否登陆成功
    int Authority = 0;//默认构造令其等于0
    
    //文件夹的html buffer
    char m_dir_html_buf[DIR_HTML_BUFFER_SIZE];
};

#endif















