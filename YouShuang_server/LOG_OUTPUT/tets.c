#include <stdio.h>
#include <string.h>
int main()
{
    char buffer[50];
    char* s = "runoobcom";
    char buffer2[6];
    snprintf(buffer2,6,"asdfg");
    printf("1buffer2 = %s\n", buffer2);
    strncpy(buffer2, "ui", 3);
    printf("2buffer2 = %s\n", buffer2);
    char buffer3[60];
    snprintf(buffer3,40,"asdfgdjfalsjdfhioasjidfl");
    snprintf(buffer, 49, "%s", buffer3);
    printf("buffer3--->buffer, buffer = %s", buffer);

    // 读取字符串并存储在 buffer 中
    int j = snprintf(buffer, 9, "%s", s);
 
    // 输出 buffer及字符数
    printf("string:\n%s\ncharacter count = %d\n", buffer, j);
 
    return 0;
}
