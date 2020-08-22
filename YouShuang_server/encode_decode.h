#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>

//16进制转为10进制
int hexit(char c){
    if(c >= '0' && c <= '9')
        return c - '0';
    if(c >= 'a' && c <= 'f')
        return  10 + c - 'a';
    if(c >= 'A' && c <= 'F')
        return  10 + c - 'A';
    return 0;
}
//编码工作是代替浏览器来对汉字进行编码，编码是对请求文件名进行编码，不让url直接对汉字进行编码
void encode_str(char *to, int tosize, const char *from){
    int tolen;
    for(tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from){
        if(isalnum(*from) || strchr("/_.-~", *from) != (char *)0){
            *to = *from;
            ++to;
            ++tolen;
        }
        else{
            sprintf(to, "%%%02x", (int)*from & 0xff);
            to += 3;
            tolen += 3;
        }
    }
    *to = '\0';
}
void decode_str(char *to, char *from){//交换两个argument位置一样？
    for( ; *from != '\0'; ++to, ++from ){
        if(from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])){
            *to = hexit(from[1])*16 + hexit(from[2]);
            from += 2;
        }
        else{
            *to = *from;
        }
    }
    *to = '\0';
}
