
/*
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(int);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
/*
一个请求引起了返回服务器端口的accept()一次调用
适当的处理请求
参数：连接到客户端的socket套接字
*/
void accept_request(int client)
{
 char buf[1024];
 int numchars;
 char method[255];
 char url[255];
 char path[512];
 size_t i, j;
 struct stat st;
 int cgi = 0;      /* becomes true if server decides this is a CGI
                    * program */
 char *query_string = NULL;

 numchars = get_line(client, buf, sizeof(buf));//从client 套接字中读取一行到buf所指内存中
 i = 0; j = 0;
 while (!ISspace(buf[j]) && (i < sizeof(method) - 1))//如果buf[j]为空格字符，则返回非0 否则 返回 0；
 {
  method[i] = buf[j];//字符数组buf剔除掉空格后 存储到method数组中 最多存储  sizeof(method)-1    个字符 
  i++; j++;
 }
 method[i] = '\0';

 if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))//既不是"GET"方法 也不是"POST"方法
 {
  unimplemented(client);
  return;
 }

 if (strcasecmp(method, "POST") == 0)  //post 方法 
  cgi = 1;

 i = 0;
 while (ISspace(buf[j]) && (j < sizeof(buf)))
  j++;//在buf数组中找到第一个非空的 字符 buf[j]
 while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
 {
  url[i] = buf[j];//buf数组剔除掉空格字符后 存储到 url数组中 
  i++; j++;
 }
 url[i] = '\0';

 if (strcasecmp(method, "GET") == 0)  //get 方法
 {
  query_string = url;
  while ((*query_string != '?') && (*query_string != '\0'))
   query_string++;
  if (*query_string == '?')
  {
   cgi = 1;
   *query_string = '\0';
   query_string++;
  }
 }

 sprintf(path, "htdocs%s", url);
 if (path[strlen(path) - 1] == '/') //path数组以'/'结尾
  strcat(path, "index.html");       //将"index.html"连接在path数组后面
 if (stat(path, &st) == -1) {  
/*
  定义函数:    int stat(const char *file_name, struct stat *buf);
  函数说明:    通过文件名filename获取文件信息，并保存在buf所指的结构体stat中
  返回值:      执行成功则返回0，失败返回-1，错误代码存于errno
*/
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
  not_found(client);
 }
 else
 {
  if ((st.st_mode & S_IFMT) == S_IFDIR)
   strcat(path, "/index.html");
  if ((st.st_mode & S_IXUSR) ||
      (st.st_mode & S_IXGRP) ||
      (st.st_mode & S_IXOTH)    )
   cgi = 1;
  if (!cgi)
   serve_file(client, path);
  else
   execute_cgi(client, path, method, query_string);
 }

 close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "<P>Your browser sent a bad request, ");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "such as a POST without a Content-Length.\r\n");
 send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
 char buf[1024];
 /*
 从文件结构体指针stream中读取数据，每次读取一行。读取的数据保存在buf指向的字符数组中，
 每次最多读取bufsize-1个字符（第bufsize个字符赋'\0'），如果文件中的该行，不足bufsize个字符
 ，则读完该行就结束。如若该行（包括最后一个换行符）的字符数超过bufsize-1，则fgets只返回一个不完整的行，
 但是，缓冲区总是以NULL字符结尾，对fgets的下一次调用会继续读该行。函数成功将返回buf，失败或读到文件结尾返回NULL。
 因此我们不能直接通过fgets的返回值来判断函数是否是出错而终止的，应该借助feof函数或者ferror函数来判
 
 函数原型
 
char *fgets(char *buf, int bufsize, FILE *stream);
参数
*buf: 字符型指针，指向用来存储所得数据的地址。
bufsize: 整型数据，指明存储数据的大小。
*stream: 文件结构体指针，将要读取的文件流。

返回值

成功，则返回第一个参数buf；
在读字符时遇到end-of-file，则eof指示器被设置，如果还没读入任何字符就遇到这种情况，则buf保持原来的内容，返回NULL；
如果发生读入错误，error指示器被设置，返回NULL，buf的值可能被改变。
 */
 fgets(buf, sizeof(buf), resource);//从文件结构体指针stream中读取数据，每次读取一行。读取的数据保存在buf指向的字符数组中，
 while (!feof(resource))
 {
  send(client, buf, strlen(buf), 0);//从client所指向的套接字描述符中发送 strlen(buf)个字节的数据到 
 //buf 所指向的内存中
  fgets(buf, sizeof(buf), resource);
 }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");//
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
 perror(sc);
 exit(1);//exit(0) 正常退出 其它非正常退出    
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
 char buf[1024];
 int cgi_output[2];
 int cgi_input[2];
 pid_t pid;
 int status;
 int i;
 char c;
 int numchars = 1;
 int content_length = -1;

 buf[0] = 'A'; buf[1] = '\0';
 if (strcasecmp(method, "GET") == 0)
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
 else    /* POST */
 {
  numchars = get_line(client, buf, sizeof(buf));
  while ((numchars > 0) && strcmp("\n", buf))
  {
   buf[15] = '\0';
   if (strcasecmp(buf, "Content-Length:") == 0)
    content_length = atoi(&(buf[16]));
   numchars = get_line(client, buf, sizeof(buf));
  }
  if (content_length == -1) {
   bad_request(client);
   return;
  }
 }

 sprintf(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);

 if (pipe(cgi_output) < 0) {
  cannot_execute(client);
  return;
 }
 if (pipe(cgi_input) < 0) {
  cannot_execute(client);
  return;
 }

 if ( (pid = fork()) < 0 ) {
  cannot_execute(client);
  return;
 }
 if (pid == 0)  /* child: CGI script */
 {
  char meth_env[255];
  char query_env[255];
  char length_env[255];

  dup2(cgi_output[1], 1);
  dup2(cgi_input[0], 0);
  close(cgi_output[0]);
  close(cgi_input[1]);
  sprintf(meth_env, "REQUEST_METHOD=%s", method);
  putenv(meth_env);
  if (strcasecmp(method, "GET") == 0) {
   sprintf(query_env, "QUERY_STRING=%s", query_string);
   putenv(query_env);
  }
  else {   /* POST */
   sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
   putenv(length_env);
  }
  execl(path, path, NULL);
  exit(0);
 } else {    /* parent */
  close(cgi_output[1]);
  close(cgi_input[0]);
  if (strcasecmp(method, "POST") == 0)
   for (i = 0; i < content_length; i++) {
    recv(client, &c, 1, 0);
    write(cgi_input[1], &c, 1);
   }
  while (read(cgi_output[0], &c, 1) > 0)
   send(client, &c, 1, 0);

  close(cgi_output[0]);
  close(cgi_input[1]);
  waitpid(pid, &status, 0);
 }
}

//**********************************************************************/
/*
       从套接字获取一行,无论该行以换行符，回车符，还是CRLF组合结尾。
当读到一个空字符时终止读该字符串。如果在缓冲区结束之前没有找到换行符，
则字符串将以null结尾。如果读取了上述终结符中的任何一个，则字符串的最
后一个字符为换行符，并且字符串将以空字符终止。

Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
//**********************************************************************/

int get_line(int sock, char *buf, int size)//参数
 /*
 sock:socket描述符
 buf:存储数据的缓冲区
 size:缓冲区的大小
 返回：存储的字节数
 */
{
 int i = 0;
 char c = '\0';
 int n;

 while ((i < size - 1) && (c != '\n'))
 {
  n = recv(sock, &c, 1, 0);//一次接收一个字节的数据
  /* DEBUG printf("%02X\n", c); */
  if (n > 0)
  {
   if (c == '\r')
   {
    n = recv(sock, &c, 1, MSG_PEEK);
    /* DEBUG printf("%02X\n", c); */
    if ((n > 0) && (c == '\n'))
     recv(sock, &c, 1, 0);
    else
     c = '\n';
   }
   buf[i] = c;
   i++;
  }     
    
  else
   c = '\n';
 }
 buf[i] = '\0';
 
 return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
 char buf[1024];
 (void)filename;  /* could use filename to determine file type */

 strcpy(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "your request because the resource specified\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "is unavailable or nonexistent.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
 FILE *resource = NULL;
 int numchars = 1;
 char buf[1024];

 buf[0] = 'A'; buf[1] = '\0';
 while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
  numchars = get_line(client, buf, sizeof(buf));

 resource = fopen(filename, "r");
 if (resource == NULL)
  not_found(client);
 else
 {
  headers(client, filename);
  cat(client, resource);
 }
 fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
/*
功能：此函数开启在指定端口监听web连接的过程。如果端口为0，则动态分配端口并修改原始端口变量以反映实际端口。

参数：指向包含要连接的端口的变量的指针

返回：套接字
*/
int startup(u_short *port) //服务端初始化监听
{
 int httpd = 0;
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0);
 if (httpd == -1)
  error_die("socket");
 memset(&name, 0, sizeof(name));
 name.sin_family = AF_INET;
 name.sin_port = htons(*port);
 name.sin_addr.s_addr = htonl(INADDR_ANY);
 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
  error_die("bind");
 if (*port == 0)  /* if dynamically allocating a port */
 {
  int namelen = sizeof(name);
  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
   error_die("getsockname");
  *port = ntohs(name.sin_port);
 }
 if (listen(httpd, 5) < 0)
  error_die("listen");
 return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
 /*
 ssize_t send(int sockfd, const void *buff, size_t nbytes, int flags);
 
 参数1：指定发送端套接字描述符
 参数2：存放要发送数据的缓冲区
 参数3：实际要发送数据的字节数
 参数4：一般置为 0
 
 <1>send先比较发送数据的长度nbytes和套接字sockfd的发送缓冲区的长度，
 如果nbytes > 套接字sockfd的发送缓冲区的长度, 该函数返回SOCKET_ERROR;
 
 <2>如果nbtyes <= 套接字sockfd的发送缓冲区的长度，
 那么send先检查协议是否正在发送sockfd的发送缓冲区中的数据，
 如果是就等待协议把数据发送完，如果协议还没有开始发送sockfd的发送缓冲区中的数据
 或者sockfd的发送缓冲区中没有数据，那么send就比较sockfd的发送缓冲区的剩余空间和nbytes
 
 <3>如果 nbytes > 套接字sockfd的发送缓冲区剩余空间的长度，
 send就等待协议把套接字sockfd的发送缓冲区中的数据发送完
 
 <4>如果 nbytes < 套接字sockfd的发送缓冲区剩余空间大小，
 send就仅仅把buf中的数据copy到剩余空间里
 (注意并不是send把套接字sockfd的发送缓冲区中的数据传到连接的另一端的，
 而是协议传送的，send仅仅是把buf中的数据copy到套接字sockfd的发送缓冲区的剩余空间里)。
 
 <5>如果send函数copy成功，就返回实际copy的字节数，如果send在copy数据时出现错误，
 那么send就返回SOCKET_ERROR; 
 如果在等待协议传送数据时网络断开，send函数也返回SOCKET_ERROR。
 
 <6>
  send函数把buff中的数据成功copy到sockfd的改善缓冲区的剩余空间后它就返回了，
  但是此时这些数据并不一定马上被传到连接的另一端。
  如果协议在后续的传送过程中出现网络错误的话，
  那么下一个socket函数就会返回SOCKET_ERROR。
  （每一个除send的socket函数在执行的最开始总要先等待套接字的发送缓冲区中的数据被协议传递完毕才能继续，
  如果在等待时出现网络错误那么该socket函数就返回SOCKET_ERROR）
  
 <7>
 在unix系统下，如果send在等待协议传送数据时网络断开，
 调用send的进程会接收到一个SIGPIPE信号，进程对该信号的处理是进程终止。
 */
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</TITLE></HEAD>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
 int server_sock = -1;
 u_short port = 0;
 int client_sock = -1;
 struct sockaddr_in client_name;
 int client_name_len = sizeof(client_name);
 pthread_t newthread;

 server_sock = startup(&port);
 printf("httpd running on port %d\n", port);

 while (1)
 {
  client_sock = accept(server_sock,
                       (struct sockaddr *)&client_name,
                       &client_name_len);
  /*
  服务器调用accept()函数接收请求，建立连接完毕。
  
  参数1：服务器的socket描述字
  参数2：指向struct sockaddr 的指针，用于返回客户端的地址
  参数3：协议地址的长度
  
  返回：由内核自动生成的一个全新的描述字，代表与返回客户的TCP连接。
  
  */
  if (client_sock == -1)
   error_die("accept");
 /* accept_request(client_sock); */
 if (pthread_create(&newthread , NULL, accept_request, client_sock) != 0)
   perror("pthread_create");
 }

 close(server_sock);

 return(0);
}
