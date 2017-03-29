#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
 int sockfd;
 int len;
 struct sockaddr_in address;
 int result;
 char ch = 'A';

 sockfd = socket(AF_INET, SOCK_STREAM, 0);//创建一个socket 
 address.sin_family = AF_INET;//ipv4
 address.sin_addr.s_addr = inet_addr("127.0.0.1");//将点分十进制的IP地址转换为网络字节序的地址
 address.sin_port = htons(9734);//端口转换成网络字节序
 len = sizeof(address);
 /*
 int connect (int sockfd, struct sockaddr * serv_addr, int addrlen);
 功能描述：将参数sockfd的socket联至参数serv_addr指定的网络地址

 参数1   ：套接字描述符
 参数2   ：指向数据结构sockaddr的指针，其中包括目的端口和IP地址
 参数3   ：参数2 sockaddr 长度，可通过sizeof()取得
 
 返回值：
          成功返回非0 失败返回0，错误码GetLastError().
 */
 result = connect(sockfd, (struct sockaddr *)&address, len);

 if (result == -1)
 {
  perror("oops: client1");
  exit(1);
 }
 write(sockfd, &ch, 1);
 /*  把参数 ch所指向的内存 写入 1个字节 到参数sockfd所指向的文件内，文件读写位置会随之移动。
     返回： 实际写入的字节数。当有错误发生时则返回-1，错误代码存入errno中.
 */
 read(sockfd, &ch, 1);
 /*
 把参数sockfd所指向的文件传送1个字节到ch指针所指向的内存中。
 */
 printf("char from server = %c\n", ch);
 close(sockfd);//关闭sockfd套接字描述字
 exit(0);
}
