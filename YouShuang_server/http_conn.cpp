
/*http_conn类的实现*///任务类
#include <map>
#include "http_conn.h"
#include <mysql/mysql.h>
#include "log.h"
#include <dirent.h>
#include "encode_decode.h"
//有些是定义在类外的变量或者函数

/*定义HTTP响应的的一些状态信息*/
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

/*网站的根目录*/
//const char* doc_root = "/var/www/html";//改之前
const char* doc_root = "/home/xky/web_server_folder/YouShuang_server/root";

//将表中的用户名和密码放到map中//全局的,每个任务类都可以访问
map<string, string> users;
locker m_lock;
connection_pool *connPool_copy = NULL;

//使用RALL机制，先从连接池中选择一个连接，
//去连接数据库，获取数据库的全部信息,放到user中，initmysql_result函数结束就释放这个对象，也就将连接放回连接之中了
void http_conn::initmysql_result(connection_pool *connPool){
    connPool_copy = connPool;
    mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，和passwd字段
    //=======================================又是一个bug
    //if(mysql_query(mysql, "SELECT username, passed FROM user")){//
    if(mysql_query(mysql, "SELECT username, passwd FROM user")){//
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
        ////char *mysql_error(MYSQL *connection); （文本错误信息）
    }
    //将查询到的结果store到结果集中去
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数,应该就两列
    int num_fields = mysql_num_fields(result);//没有用到
    
    //获取表头，即字段，返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);//没有用到

    //从结果集中获取下一行，将对应的用户名和密码，放到map中users；
    while(MYSQL_ROW row = mysql_fetch_row(result)){ //typedef char** MYSQL_ROW

        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
        printf("%s %s\n", temp1.c_str(), temp2.c_str());
    }

}

//将文件描述符设置为非阻塞的，好像要用epoll的ET+非阻塞模式
int setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option); //=======================================================犯的第一个错误
    return old_option;
}

//将文件描述符加入epoll树上
void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;//EPOLLRDHUP是2.6.17后才有的事件，对段关闭触发事件EPOLLIN和EPOLLRDHUP。
    if(one_shot){
        event.events |= EPOLLONESHOT;//多线程中要用oneshot，
        //通过时延控制短时间内的数据都被一个线程接收处理，以免程序接收数据出错。比如一个http请求消息。只能被一个线程全部接收。
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}
//epoll树上删除fd
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);//传0和传NULL一样的在c++中 #define NULL 0
    //传NULL在c中表示#define NULL ((void*)0)//c++中int *p = (void*)0;出错，因为c++中是严格的类型转换要求。
    close(fd);
}

//对于oneshot的fd，重置fd到epoll树上
void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//初始化http_conn类的两个静态成员数据
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close){
    char ipbuf[INET_ADDRSTRLEN];
    printf("Close client ip:%s, port:%d, fd:%d\n",inet_ntop(AF_INET, &m_address.sin_addr.s_addr, 
                                                            ipbuf, sizeof(ipbuf)), ntohs(m_address.sin_port), m_sockfd);
    if(real_close && (m_sockfd != -1)){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;//表明它是已经被释放的文件描述符，因为文件描述符0-1023
        m_user_count --;//连接的http客户减少一个，所有任务都共享的静态数据。//？？？？？？？应该要加锁吧？？？？
    }
}

//定义公有的init函数
void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;
    /*如下两行是为了避免TIME_WAIT状态，仅用于调试，实际使用中应该去掉*/
    //int reuse = 1;
    //setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, m_sockfd, true);
    m_user_count++;

    //调用私有方法，只能在类的函数中调用，外部不能直接调用
    init();

}

//定义私有的init函数
void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;//先是请求行再是请求头最后请求体，这是主状态机状态的转换，每个任务不一样。
    m_linger = false;//该http请求是否要求保持连接

    m_method = GET;//先默认初始化为GET
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;
    //
    cgi = 0;
    mysql = NULL;
    //用户读缓存区
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    //用户写缓存区
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
    memset(m_dir_html_buf, '\0', DIR_HTML_BUFFER_SIZE);
}

/*从状态机*/
//一行行地分析缓冲区中的一行是否完整，并将r n置为'\0'
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    /*m_read_idx 标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置*/
    /*m_checked_idx当前正在分析的字符在读缓冲区中位置*/
    for( ; m_checked_idx < m_read_idx; ++m_checked_idx){
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r'){
            if((m_checked_idx + 1) == m_read_idx){
                return LINE_OPEN; //表示还要再读,等待client再次发来数据，因为非阻塞读是将内核读缓冲区数据一次性读完了的，并且是ONESHOT
                //所以要重置sockfd，等待下一次数据到来，读取的数据不完整，当前用户读缓冲区m_read_buf数据不够，
                //应该再从fd的内核缓冲区中读取数据
            }
            else if(m_read_buf[m_checked_idx + 1] == '\n'){ //表示用户缓冲区中行读取完整
                m_read_buf[m_checked_idx++] = '\0';//将'\r'设为'\0'
                m_read_buf[m_checked_idx++] = '\0'; //将'\n'设为'\0'm_checked_idx指向缓存区中\n的下一个位置
                return LINE_OK;
            }
            return LINE_BAD;//就是'\r'后面不是'\n'
        }
        else if(temp == '\n'){//???????????????????????
            //感觉下面这个if可以不要？？？？？？？？？？？？？必须要就是前面读'\r'然后返回LINE_OPEN,下一次读就是‘/n’开头。
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')){
                m_read_buf[m_checked_idx-1] = '\0';//将'\r'设为'\0'
                m_read_buf[m_checked_idx++] = '\0'; //将'\n'设为'\0'm_checked_idx指向缓存区中\n的下一个位置
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    //把缓冲区都遍历完了都没有返回LINE_OK,就是应该等client下一次发来后继数据。
    return LINE_OPEN;
}

/*循环读客户数据，直到无数据可读或者对方关闭连接*/
/*非阻塞读操作*/
//好像是用同步IO模拟的proator事件处理模式，所以这个是主线程来调用

//这里来更新m_read_idx,通过主线程来执行这个函数
bool http_conn::read(){
    printf("start read\n");
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read = 0;
    int i = 1;
    while(true){
        printf("循环读第%d次\n", i);
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        printf("循环读第%d次结束\n", i++);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){//读socketfd时，这两个要一起用，
                printf("read filish\n");
                break;//读完了，返回true
                //应该重置cfd。因为ONESHOT？？？？？？？？？？？？？？？？？？？？
            }
            perror("recv error");
            return false;//不然就是出错
        }
        else if(bytes_read == 0){
            printf("client close\n");
            return false;//对端关闭, 应该关闭通信的cfd//？？？？？？？？？？？？？？？？？？？？
        }
        //这里不用延时吗，因为用了ONESHOT//？？？？？？？？？？？？？？？？？？？
        m_read_idx += bytes_read;
    }
    return true;
}

/*解析HTTP请求行，获取请求方法， 目标URL， 以及HTTP版本号*/
http_conn::HTTP_CODE http_conn::parse_request_line(char *text){//text表示请求中的一行
    m_url = strpbrk(text, " \t");//返回2中任意字符，最先在1中出现的位置//
    if(!m_url) return BAD_REQUEST;//请求行数据不对
    *(m_url++) = '\0';//将\t或者空格设为'\0'

    char* method = text;
    if(strcasecmp(method, "GET") == 0){
        m_method = GET;
    }
    else if(strcasecmp(method, "POST") == 0){
        m_method = POST;
        cgi = 1;
    }
    else{
        return BAD_REQUEST;//只接收GET请求
    }

    //掠过空格和\t
    m_url += strspn(m_url, " \t");//检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_version = strpbrk(m_url, " \t");
    if(!m_version){
        return BAD_REQUEST;
    }
    *(m_version++) = '\0';
    
    printf("m_url1 = %s\n", m_url);
    
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }
    
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;
        m_url = strchr(m_url, '/');

    }
    printf("m_url2 = %s\n", m_url);
    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;//设置主状态机的状态，这里发生状态的变化
    //好像要整个请求解析完才知道是不是GET_REQUEST
    return NO_REQUEST;//为什么不返回GET_REQUEST,好像只要不返回BAD_REQUEST就可以了????????????????????
}

/*解析HTTP请求头*/
http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    /*遇到空行，表示头部信息已经解析完毕*/
    if(text[0] == '\0'){
        /*如果HTTP请求有消息体，则还需要读取m_content_length字节的请求体，状态机转移到CHECK_STATE_CONTENT状态*/
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;//好像要整个请求解析完才知道是不是GET_REQUEST
        }
        /*否则表示我们已经得到一个完整的HTTP请求*/
        return GET_REQUEST;//确实是要解析完整个HTTP请求才返回GET_REQUEST

    }
    /*处理connection头部字段*/
    else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0){
            m_linger = true;
        }
    }
    /*处理Content-Length头部字段*/
    else if(strncasecmp(text, "Content-Length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    }
    /*处理Host头部字段*/
    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else{
        printf("oop! unkonw header %s\n", text);//不关心其他字段
    }
    return NO_REQUEST;
}

/*我们没有真正的解析HTTP的消息体，只是判断它是否被完整的读入了*///通过content-length
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if(m_read_idx >= (m_content_length + m_checked_idx)){
        text[m_content_length] = '\0';
        //对于post来说请求体中含有数据，
        m_string = text;
        return GET_REQUEST;//确实是一个完整的HTTP的GET请求
    }
    return NO_REQUEST; //当前缓冲区没有包含完整的请求。等待下一次读fd的读缓冲区。（数据到来时）。
}
/*主状态机*/
http_conn::HTTP_CODE http_conn::process_read(){
    //定义三个局部变量
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || 
          ((line_status = parse_line()) == LINE_OK)){
        //text是某行开始的位置，或者是请求体开始的位置
        text = get_line();//char* get_line() {return m_read_buf + m_start_line;};
        m_start_line = m_checked_idx;//将m_start_line赋值为下一行开始的位置
        printf("got 1 http line in request:%s\n", text);//后面的rn已经在parse_line()中变为‘/0’

        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
                {
                    ret = parse_request_line(text);
                    if(ret == BAD_REQUEST){
                        return BAD_REQUEST;
                    }
                    break;
                }
            case CHECK_STATE_HEADER:
                {
                    ret = parse_headers(text);
                    if(ret == BAD_REQUEST){
                        return BAD_REQUEST;
                    }
                    else if(ret == GET_REQUEST){
                        return do_request();
                    }
                    break;
                }
            case CHECK_STATE_CONTENT:
                {
                    ret = parse_content(text);
                    printf("m_url3 = %s\n", m_url);
                    if(ret == GET_REQUEST){
                        return do_request();
                    }
                    line_status = LINE_OPEN;
                    break;
                }
            default:
                {
                    return INTERNAL_ERROR;
                }
        }
    }
    return NO_REQUEST;//表示还没完整的HTTP请求
}

/*当得到一个完整的、正确的HTTP请求时， 
 * 我们就分析目标文件的属性，如果目标文件存在，对所有的用户可读，且不是目录，
 * 则使用mmap将其映射到内存地址m_file_address,并告诉调用者文件获取成功 */
http_conn::HTTP_CODE http_conn::do_request(){
/*
 * 网站的根目录20 const char* doc_root = "/var/www/html";//改为./root,因为mmap失败，所以改
 *const char* doc_root = "/home/xky/web_server_folder/YouShuang_server/root";
 */
    printf("m_url4 = %s\n", m_url);
    //strcpy(m_read_buf, doc_root);//=====================================================第二个错误
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //m_url是去除了http://过后的
    printf("m_url5 = %s--------\n", m_url);
    
    const char* p = strrchr(m_url, '/');
    
    //处理cgi
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')){
        //根据标志来判断是登陆检测还是注册检测
        char flag = m_url[1]; ////m_url == /   or    /0   or   /1  or    /2CGISQL      or     /3CGISQL
        char *m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);

        //将用户名和密码提取出来
        //user = 123  && passwd =123
        char name[100], password[100];
        int i;
        for(i = 5; m_string[i] != '&'; ++i){//user=xky&password=123
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        for(i = i + 10; m_string[i] != '\0'; ++i, ++j){
            password[j] = m_string[i];
        }
        password[j] = '\0';

        //同步线程注册检验
        if(*(p + 1) == '3'){
            printf("这里*(p + 1) == '3'，name = %s, passwd = %s--------------\n", name, password);
            //先检测数据库中是否有重名
            //没有重名的，就增加数据
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            if(users.find(name) == users.end()){
                //MYSQL *mysql = connection_pool::GetInstance()->GetConnection();
                mysql = NULL;
                connectionRAII mysqlcon(&mysql, connPool_copy);

                m_lock.lock(); //加锁是为了，如果有多个客户端都在访问数据资源，避免数据混乱要进行加锁
                int res = mysql_query(mysql, sql_insert);
                users.insert(make_pair(name, password));
                m_lock.unlock();

                if(!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
            free(sql_insert);
        }
        //如果是登陆
        if(*(p + 1) == '2'){
            if(users.find(name) != users.end() && users[name] == password){
                strcpy(m_url, "/welcome.html");
                Authority = 1;
            }
            else
                strcpy(m_url, "/logError.html");
        }
    }

    printf("000000000-------\n");
    printf("cgi = %d, *(p + 1) = %c\n", cgi, *(p + 1));
    printf("m_real_file  =  %s\n", m_real_file);
    printf("000000000--------\n");
    if(cgi == 1 && *(p + 1) == '0'){
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        printf("0m_real_file  =  %s\n", m_real_file);
        free(m_url_real);
    }
    else if(cgi == 1 && *(p + 1) == '1'){
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        printf("1m_real_file  =  %s\n", m_real_file);
        free(m_url_real);
    }
    else if(cgi == 1 && *(p + 1) == '5'){
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);
    }
    else if(cgi == 1 && *(p + 1) == '6'){
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);
    }
    else if(cgi == 1 && *(p + 1) == '7'){
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);
    }
    else{//那么就是get请求了
        printf("m_url  %s\n================", m_url);
        if(strcmp("/", m_url) == 0)
            strncpy(m_real_file + len, "/judge.html", FILENAME_LEN - len - 1);
            //strncpy(m_real_file + len, "/xky_file_system/", FILENAME_LEN - len - 1);
        /*else if(strncmp(m_url, "/xky_file_system", 16) == 0 && Authority == 1){
            strncpy(m_real_file + len, "/xky_file_system/", FILENAME_LEN - len - 1);
        }else if(Authority == 1)
            strncpy(m_real_file + len, m_url, FILENAME_LEN - len -1);
        else{
            return FORBIDDEN_REQUEST;
        }*/
        else strncpy(m_real_file + len, m_url, FILENAME_LEN - len -1);
    }
    //printf
    printf("m_real_file ============= %s\n", m_real_file);
    //------------------
    if(stat(m_real_file, &m_file_stat) < 0){
        //404
        return NO_RESOURCE; 
    }
    if(!(m_file_stat.st_mode & S_IROTH)){
        //403
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST; //连文件夹都不支持？？？给你加上一个功能
        /*write_dir_html(m_real_file);    
        return DIR_REQUEST; //连文件夹都不支持？？？给你加上一个功能*/
    }
    //下面就是普通文件了
    int fd = open(m_real_file, O_RDONLY);
    if(fd == -1){
        perror("open error");
        return BAD_REQUEST;
    }
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(m_file_address == MAP_FAILED){
        perror("mmap  error");
        return BAD_REQUEST;

    }
    printf("mmap sucessed\n");
    close(fd);
    return FILE_REQUEST;
}

/*对内存映射区执行munmap()*/
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
/*
//发送目录html
void http_conn::write_dir_html(const char* dirname){
    //拼一个html页面<table></table>
    char buf[1024] = {0};
    memset(m_dir_html_buf, '\0', DIR_HTML_BUFFER_SIZE);
    sprintf(buf, "<!doctype html><html><head><title>目录名:%s</title></head>", dirname);
    sprintf(buf+strlen(buf), "<body><h1>当前目录:%s</h1><table>", dirname);

    char enstr[1024] = {0};
    char path[1024] = {0};
    //目录项二级指针
    //头文件#include <dirent.h>
    
    // scandir函数读取特定目录数据
     //然后将读到的目录中的项（将信息写道结构体中struct dirent），将结构体的指针放到函数动态开辟的数组中，返回数组名给ptr
    
    struct dirent** ptr;
    //num返回此目录下有多少文件（普通文件和目录）
    int num = scandir(dirname, &ptr, NULL, alphasort);
    //遍历
    for(int i = 0; i < num; i++){
        char *name = ptr[i]->d_name;
        //拼接文件的完整路径
        sprintf(path, "%s%s", dirname, name);
        printf("path = %s ====================\n", path);
        struct stat st;
        stat(path, &st);
        //将name编码，让浏览器请求的时候，url请求中是编码后的请求内容名称
        encode_str(enstr, sizeof(enstr), name);
        //如果当前遍历到文件
        if(S_ISREG(st.st_mode)){
            sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\" title=\"%s\">%s</a></td><td>%ld</td></tr>", enstr, enstr, name, (long)st.st_size);
        }
        else if(S_ISDIR(st.st_mode)){
            sprintf(buf+strlen(buf), "<tr><td><a href=\"%s/\" title=\"%s\">%s/</a></td><td>%ld</td></tr>", enstr, enstr, name, (long)st.st_size);
        }
        //send(cfd, buf, strlen(buf), 0);
        strncpy(m_dir_html_buf + strlen(m_dir_html_buf), buf, DIR_HTML_BUFFER_SIZE - strlen(m_dir_html_buf) - 1);
        //对于不同的文件要清空buf
        memset(buf, 0, sizeof(buf));
    }
    sprintf(buf+strlen(buf), "</table></body></html>");
    strncpy(m_dir_html_buf + strlen(m_dir_html_buf), buf, DIR_HTML_BUFFER_SIZE - strlen(m_dir_html_buf) - 1);
    //send(cfd, buf, strlen(buf), 0);
    printf("write dir_html_buf OK!!!!\n");
}*/
//发送目录html
void http_conn::write_dir_html(const char* dirname){
    //拼一个html页面<table></table>
    char buf[1024] = {0};
    memset(m_dir_html_buf, '\0', DIR_HTML_BUFFER_SIZE);
    sprintf(buf, "<!doctype html><html><head><meta charset=\"UTF-8\"><title>目录名:%s</title></head>", dirname);
    sprintf(buf+strlen(buf), "<body><h1>当前目录:%s</h1><br/><table>", dirname);

    char enstr[1024] = {0};
    char path[1024] = {0};
    //目录项二级指针
    //头文件#include <dirent.h>
    /*
     scandir函数读取特定目录数据
     然后将读到的目录中的项（将信息写道结构体中struct dirent），将结构体的指针放到函数动态开辟的数组中，返回数组名给ptr
     */
    struct dirent** ptr;
    //num返回此目录下有多少文件（普通文件和目录）
    int num = scandir(dirname, &ptr, NULL, alphasort);
    //遍历
    for(int i = 0; i < num; i++){
        char *name = ptr[i]->d_name;
        //拼接文件的完整路径
        sprintf(path, "%s%s", dirname, name);
        printf("path = %s ====================\n", path);
        struct stat st;
        stat(path, &st);
        //将name编码，让浏览器请求的时候，url请求中是编码后的请求内容名称
        encode_str(enstr, sizeof(enstr), name);
        //如果当前遍历到文件
        if(S_ISREG(st.st_mode)){
            sprintf(buf + strlen(buf), "<tr><td><form action=\"%s\" method=\"post\"><div align=\"center\"><button type=\"submit\">%s</button></div></form>\
                    </td><td>%ld</td></tr>", path, name, (long)st.st_size);
        }
        else if(S_ISDIR(st.st_mode)){
            sprintf(buf+strlen(buf), "<tr><td><form action=\"%s/\" method=\"post\"><div align=\"center\"><button type=\"submit\">%s</button></div></form>\
                    </td><td>%ld</td></tr>", path, name, (long)st.st_size);
        }
        //send(cfd, buf, strlen(buf), 0);
        strncpy(m_dir_html_buf + strlen(m_dir_html_buf), buf, DIR_HTML_BUFFER_SIZE - strlen(m_dir_html_buf) - 1);
        //对于不同的文件要清空buf
        memset(buf, 0, sizeof(buf));
    }
    sprintf(buf+strlen(buf), "</table></body></html>");
    strncpy(m_dir_html_buf + strlen(m_dir_html_buf), buf, DIR_HTML_BUFFER_SIZE - strlen(m_dir_html_buf) - 1);
    //send(cfd, buf, strlen(buf), 0);
    printf("write dir_html_buf OK!!!!\n");
}
/*写HTTP响应*/
//好像是用同步IO模拟的proator事件处理模式，所以这个是主线程来调用
bool http_conn::write(){
    int temp = 0;
    printf("bytes_to_send = %d\n", bytes_to_send); //m_write_idx由工作线程来根新，主线程调用write会用到m_write_idx;
    printf("bytes_have_send = %d\n", bytes_have_send);
    if(bytes_to_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();//把读事件发生时，改变的变量再次初始化
        return true;
    }
    while(1){
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1){
            /*如果TCP的内核写缓冲区没有空间，则等下一轮的EPOLLOUT事件， 虽然在这段时间内
             * ，服务器无法收到同一连接发过来的下一个请求，但可以保证一个连接的完整性*/
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }         
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            //m_iv[0].iov_len = m_write_idx - bytes_have_send;//该之前，因为发送的数据不对
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;//改之后
        }
        if(bytes_to_send <= 0){//发送成功
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if(m_linger){
                //
                printf("write end keep alive\n");
                init();
                return true;
            }
            else{
                printf("write end dont keep alive\n");
                return false;
            }
        }
    }
}


/*往写缓冲区中写入待发送的数据*///proactor
bool http_conn::add_response(const char* format,...){
    if(m_write_idx >= WRITE_BUFFER_SIZE){
        return false;//写缓冲区已满不能再写
    }
    va_list arg_list;
    va_start(arg_list, format);
    //以format格式拼接arg_list
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= WRITE_BUFFER_SIZE - 1 - m_write_idx){
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

/*写入状态行*/
bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
/*写入响应头*/
bool http_conn::add_headers(int content_len){
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

/*写入响应体长度*/
bool http_conn::add_content_length(int content_len){
    if(content_len == -1) return true;
    else return add_response("Content-Length:%d\r\n", content_len);
}

/*添加linger字段*/
bool http_conn::add_linger(){
    printf("send Connection: %s\n", (m_linger == true)?"keep-alive":"close");
    return add_response("Connection:%s\r\n", (m_linger == true)?"keep-alive":"close");
}

/*添加空行*/
bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}
/*添加响应体段*///在有些请求下，文件请求就不用这个了
bool http_conn::add_content(const char* content){
    return add_response("%s", content);
}

/* 根据服务器处理HTTP请求的结果，决定返回给客户端的内容*/

bool http_conn::process_write(HTTP_CODE ret){
    switch(ret)
    {
    case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        }
    case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
            break;
        }
    case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)){
                return false;
            }
            break;
            
        }
    case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)){
                return false;
            }
            break;

        }
    case DIR_REQUEST:
        {
            add_status_line(200, ok_200_title);
            add_headers(-1); //目录html文件全部放在m_write_buf中,不知道html文件的长度，可以先不传长度
            add_content(m_dir_html_buf);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            bytes_to_send = m_iv[0].iov_len;
            m_iv_count = 1;
            return true;

        }
    case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0){
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                printf("m_iv[0].iov_len = %d\n", (int)m_iv[0].iov_len);
                //m_iv[1].iov_base = m_real_file;//======================================================第四出错误
                m_iv[1].iov_base = m_file_address;
                //m_iv[0].iov_len = m_file_stat.st_size;//============================================第三处错误
                m_iv[1].iov_len = m_file_stat.st_size;
                printf("m_iv[1].iov_len = %d\n", (int)m_iv[1].iov_len);
                bytes_to_send = m_iv[0].iov_len + m_iv[1].iov_len;
                m_iv_count = 2;
                return true;

            }
            else{
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)){
                    return false;
                }
            }
        }
    default:
        {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    bytes_to_send = m_iv[0].iov_len;
    m_iv_count = 1;
    return true;
}

/*线程的入口函数*///process

void http_conn::process(){
    //已经读到用户区了，所以这是异步的proactor
    HTTP_CODE read_ret = process_read();
    //
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    //如果哦是其他的处理结果
    bool write_ret = process_write(read_ret);
    if(!write_ret){//想这里不出错，只能让用户写缓冲区增大
        close_conn();
    }
    //数据已经在用户写缓冲区准备好了，主线程可以开始监听cfd的写事件了。
    //写事件发生，则write函数直接将所有的数据从用户写缓冲区和mmap映射区发出去。
    modfd(m_epollfd, m_sockfd, EPOLLOUT);

    //让主线程开始监听这个cfd的写事件
}




