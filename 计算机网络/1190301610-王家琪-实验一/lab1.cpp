#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <tchar.h>
#include <map>
#include <string>
#include <set>

#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口

#define CACHE_NUM 100 //最大cache数量
using namespace std;


//在s1里面，把所有的s2替换成s3
char* replace(char* s1, char* s2, char* s3 = NULL)
{
	char* p, * from, * to, * begin = s1;
	int c1, c2, c3, c;         //串长度及计数
	c2 = strlen(s2);
	c3 = (s3 != NULL) ? strlen(s3) : 0;
	if (c2 == 0) return s1;     //注意要退出
	while (true)             //替换所有出现的串
	{
		c1 = strlen(begin);
		p = strstr(begin, s2); //s2出现位置
		if (p == NULL)         //没找到
			return s1;

		if (c2 > c3)           //串往前移
		{
			from = p + c2;
			to = p + c3;
			c = c1 - c2 + begin - p + 1;
			while (c--)
				*to++ = *from++;
		}
		else if (c2 < c3)      //串往后移
		{
			from = begin + c1;
			to = from - c2 + c3;
			c = from - p - c2 + 1;
			while (c--)
				*to-- = *from--;
		}
		if (c3)              //完成替换
		{
			from = s3, to = p, c = c3;
			while (c--)
				*to++ = *from++;
		}
		begin = p + c3;         //新的查找位置
	}
}
struct ptrCmp
{
	bool operator()(const char* s1, const char* s2) const
	{
		return strcmp(s1, s2) < 0;
	}
};
//网址名
char host[] = "jwts.hit.edu.cn";
char blank[] = "";
char host1[] = "jwes.hit.edu.cn";
char host2[] = "cs.hit.edu.cn";
//cs.hit.edu.cn 计算学部网址

//访问受限的主机名
char ip[] = "";
map<char*, char*, ptrCmp> transfer = {
	{host, blank},
	{host1, host2}
};
set<char*, ptrCmp> ban = { ip };
//Http 重要头部数据
struct HttpHeader {
	char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂不考虑
	char url[1024]; // 请求的 url
	char host[1024]; // 目标主机
	char cookie[1024 * 10]; //cookie
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));
	}
};
struct Cache {
	char url[1024];  //url地址
	char time[40];   //上次更新时间
	char buffer[MAXSIZE];   //缓存的内容
	Cache() {
		ZeroMemory(this, sizeof(Cache));
	}
}cache[CACHE_NUM];
int last_cache = 0;
const char ims[] = "If-Modified-Since: ";
BOOL InitSocket();
void ParseHttpHead(char* buffer, HttpHeader* httpHeader);
BOOL ConnectToServer(SOCKET* serverSocket, char* host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
//代理相关参数
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;
//由于新的连接都使用新线程进行处理，对线程的频繁的创建和销毁特别浪费资源
//可以使用线程池技术提高服务器效率
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};
struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};
int _tmain(int argc, _TCHAR* argv[])
{
	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket()) {
		printf("socket 初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam* lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID;
	sockaddr_in addr;
	int temp = sizeof(SOCKADDR);
	//代理服务器不断监听
	while (true) {
		acceptSocket = accept(ProxyServer, (SOCKADDR*)&addr, &temp);
		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL) {
			continue;
		}
		if (ban.find(inet_ntoa(addr.sin_addr)) != ban.end()) {
			printf("用户访问受限\n");
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		if (hThread == NULL) {
			printf("线程创建失败\n");
			continue;
		}
		CloseHandle(hThread);
		Sleep(200);
	}
	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}
//************************************
// Method: InitSocket
// FullName: InitSocket
// Access: public
// Returns: BOOL
// Qualifier: 初始化套接字
//************************************
BOOL InitSocket() {
	//加载套接字库（必须）
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();
		return FALSE;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("创建套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("绑定套接字失败\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("监听端口%d 失败", ProxyPort);
		return FALSE;
	}
	return TRUE;
}
//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: 线程执行函数
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter) {
	char Buffer[MAXSIZE];
	char* CacheBuffer;
	ZeroMemory(Buffer, MAXSIZE);
	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;
	int i = 0;
	HttpHeader* httpHeader = new HttpHeader();
	//接收报文
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}

	//复制缓存
	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);

	//解析报文头部到httpHeader
	ParseHttpHead(CacheBuffer, httpHeader);
	delete[] CacheBuffer;

	//网站过滤
	if (transfer.find(httpHeader->host) != transfer.end()) {
		//replace函数在这里用了，将buffer里面的httpHeader->host,改成transfor之后的结果
		replace(Buffer, httpHeader->host, transfer[httpHeader->host]);
		//更改httpHeader->host的主机地址
		memcpy(httpHeader->host, transfer[httpHeader->host], strlen(transfer[httpHeader->host]) + 1);
	}

	//和web服务器建立连接
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		goto error;
	}

	printf("代理连接主机 %s 成功\n", httpHeader->host);//输出建立连接成功

	//如果客户请求为GET
	if (!strcmp(httpHeader->method, "GET"))
		for (i = 0; i < CACHE_NUM; i++)
			if (strlen(cache[i].url) != 0 && !strcmp(cache[i].url, httpHeader->url)) {
				printf("cache命中，url=%s, time=%s\n", httpHeader->url, cache[i].time);
				int time_len = strlen(cache[i].time);
				recvSize -= 2;
				memcpy(Buffer + recvSize, ims, 19);
				recvSize += 19;
				memcpy(Buffer + recvSize, cache[i].time, strlen(cache[i].time));
				recvSize += strlen(cache[i].time);
				Buffer[recvSize++] = '\r';
				Buffer[recvSize++] = '\n';
				Buffer[recvSize++] = '\r';
				Buffer[recvSize++] = '\n';
				//	printf("\n--------------------Send----------------------\n");
				//	printf("%s", Buffer);
				//	printf("\n----------------------------------------------\n");
				break;
			}
	//将客户端发送的 HTTP 数据报文直接转发给目标服务器
	ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
	//等待目标服务器返回数据
	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}
	printf("\n代理从目标服务器收到的信息\n");
	printf("%s", Buffer);
	printf("\n----------------------------------------------\n");
	if (!memcmp(&Buffer[9], "304", 3)) {//决策是304
		printf("-----------------------返回缓存----------------------\n");
		ret = send(((ProxyParam*)lpParameter)->clientSocket, cache[i].buffer, sizeof(cache[i].buffer), 0);
		if (ret != SOCKET_ERROR) {
			printf("缓存返回成功\n");
		}
	}

	else {
		if (!strcmp(httpHeader->method, "GET") && !memcmp(&Buffer[9], "200", 3)) {//决策是200
			char Buffer2[MAXSIZE];
			memcpy(Buffer2, Buffer, sizeof(Buffer));
			const char* delim = "\r\n";
			char* ptr;
			char* p = strtok_s(Buffer2, delim, &ptr);//分割字符串
			bool flag = false;
			while (p) {
				if (strlen(p) >= 15 && !memcmp(p, "Last-Modified: ", 15)) {
					flag = true;
					break;
				}
				p = strtok_s(NULL, delim, &ptr);
			}
			//添加缓存
			if (flag) {
				printf("添加缓存\n");
				last_cache++;
				last_cache %= CACHE_NUM;
				memcpy(cache[last_cache].url, httpHeader->url, sizeof(httpHeader->url));
				memcpy(cache[last_cache].time, p + 15, strlen(p) - 15);
				memcpy(cache[last_cache].buffer, Buffer, sizeof(Buffer));
				printf("\n添加的缓存\n");
				printf("%s\n", cache[last_cache].url);
				printf("%s\n", cache[last_cache].time);
				printf("%s", Buffer);
				printf("\n----------------------------------------------\n");
			}
			//将目标服务器返回的数据直接转发给客户端
		}
		printf("\n代理服务器向用户发送数据\n");
		printf("%s", Buffer);
		printf("\n----------------------------------------------\n");
		ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
	}
	//错误处理
error:
	printf("关闭套接字\n");
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete lpParameter;
	_endthreadex(0);
	return 0;
}
//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public
// Returns: void
// Qualifier: 解析 TCP 报文中的 HTTP 头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char* buffer, HttpHeader* httpHeader) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//提取第一行
	printf("%s\n", p);
	if (p[0] == 'G') {//GET 方式
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {//POST 方式
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	printf("%s\n", httpHeader->url);
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'H'://Host
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				header[7] = 0;
				if (!strcmp(header, "Cookie")) {
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public
// Returns: BOOL
// Qualifier: 根据主机创建目标服务器套接字，并连接
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET* serverSocket, char* host) {
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
	HOSTENT* hostent = gethostbyname(host);
	if (!hostent) {
		return FALSE;
	}
	in_addr Inaddr = *((in_addr*)*hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET) {
		return FALSE;
	}
	if (connect(*serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}
