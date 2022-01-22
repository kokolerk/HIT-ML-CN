#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
#include <tchar.h>
#include <map>
#include <string>
#include <set>

#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65507 //�������ݱ��ĵ���󳤶�
#define HTTP_PORT 80 //http �������˿�

#define CACHE_NUM 100 //���cache����
using namespace std;


//��s1���棬�����е�s2�滻��s3
char* replace(char* s1, char* s2, char* s3 = NULL)
{
	char* p, * from, * to, * begin = s1;
	int c1, c2, c3, c;         //�����ȼ�����
	c2 = strlen(s2);
	c3 = (s3 != NULL) ? strlen(s3) : 0;
	if (c2 == 0) return s1;     //ע��Ҫ�˳�
	while (true)             //�滻���г��ֵĴ�
	{
		c1 = strlen(begin);
		p = strstr(begin, s2); //s2����λ��
		if (p == NULL)         //û�ҵ�
			return s1;

		if (c2 > c3)           //����ǰ��
		{
			from = p + c2;
			to = p + c3;
			c = c1 - c2 + begin - p + 1;
			while (c--)
				*to++ = *from++;
		}
		else if (c2 < c3)      //��������
		{
			from = begin + c1;
			to = from - c2 + c3;
			c = from - p - c2 + 1;
			while (c--)
				*to-- = *from--;
		}
		if (c3)              //����滻
		{
			from = s3, to = p, c = c3;
			while (c--)
				*to++ = *from++;
		}
		begin = p + c3;         //�µĲ���λ��
	}
}
struct ptrCmp
{
	bool operator()(const char* s1, const char* s2) const
	{
		return strcmp(s1, s2) < 0;
	}
};
//��ַ��
char host[] = "jwts.hit.edu.cn";
char blank[] = "";
char host1[] = "jwes.hit.edu.cn";
char host2[] = "cs.hit.edu.cn";
//cs.hit.edu.cn ����ѧ����ַ

//�������޵�������
char ip[] = "";
map<char*, char*, ptrCmp> transfer = {
	{host, blank},
	{host1, host2}
};
set<char*, ptrCmp> ban = { ip };
//Http ��Ҫͷ������
struct HttpHeader {
	char method[4]; // POST ���� GET��ע����ЩΪ CONNECT����ʵ���ݲ�����
	char url[1024]; // ����� url
	char host[1024]; // Ŀ������
	char cookie[1024 * 10]; //cookie
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));
	}
};
struct Cache {
	char url[1024];  //url��ַ
	char time[40];   //�ϴθ���ʱ��
	char buffer[MAXSIZE];   //���������
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
//������ز���
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;
//�����µ����Ӷ�ʹ�����߳̽��д������̵߳�Ƶ���Ĵ����������ر��˷���Դ
//����ʹ���̳߳ؼ�����߷�����Ч��
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};
struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};
int _tmain(int argc, _TCHAR* argv[])
{
	printf("�����������������\n");
	printf("��ʼ��...\n");
	if (!InitSocket()) {
		printf("socket ��ʼ��ʧ��\n");
		return -1;
	}
	printf("����������������У������˿� %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam* lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID;
	sockaddr_in addr;
	int temp = sizeof(SOCKADDR);
	//������������ϼ���
	while (true) {
		acceptSocket = accept(ProxyServer, (SOCKADDR*)&addr, &temp);
		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL) {
			continue;
		}
		if (ban.find(inet_ntoa(addr.sin_addr)) != ban.end()) {
			printf("�û���������\n");
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		if (hThread == NULL) {
			printf("�̴߳���ʧ��\n");
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
// Qualifier: ��ʼ���׽���
//************************************
BOOL InitSocket() {
	//�����׽��ֿ⣨���룩
	WORD wVersionRequested;
	WSADATA wsaData;
	//�׽��ּ���ʱ������ʾ
	int err;
	//�汾 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//���� dll �ļ� Scoket ��
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//�Ҳ��� winsock.dll
		printf("���� winsock ʧ�ܣ��������Ϊ: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("�����ҵ���ȷ�� winsock �汾\n");
		WSACleanup();
		return FALSE;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("�����׽���ʧ�ܣ��������Ϊ��%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("���׽���ʧ��\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("�����˿�%d ʧ��", ProxyPort);
		return FALSE;
	}
	return TRUE;
}
//************************************
// Method: ProxyThread
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: �߳�ִ�к���
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
	//���ձ���
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}

	//���ƻ���
	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);

	//��������ͷ����httpHeader
	ParseHttpHead(CacheBuffer, httpHeader);
	delete[] CacheBuffer;

	//��վ����
	if (transfer.find(httpHeader->host) != transfer.end()) {
		//replace�������������ˣ���buffer�����httpHeader->host,�ĳ�transfor֮��Ľ��
		replace(Buffer, httpHeader->host, transfer[httpHeader->host]);
		//����httpHeader->host��������ַ
		memcpy(httpHeader->host, transfer[httpHeader->host], strlen(transfer[httpHeader->host]) + 1);
	}

	//��web��������������
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		goto error;
	}

	printf("������������ %s �ɹ�\n", httpHeader->host);//����������ӳɹ�

	//����ͻ�����ΪGET
	if (!strcmp(httpHeader->method, "GET"))
		for (i = 0; i < CACHE_NUM; i++)
			if (strlen(cache[i].url) != 0 && !strcmp(cache[i].url, httpHeader->url)) {
				printf("cache���У�url=%s, time=%s\n", httpHeader->url, cache[i].time);
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
	//���ͻ��˷��͵� HTTP ���ݱ���ֱ��ת����Ŀ�������
	ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
	//�ȴ�Ŀ���������������
	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}
	printf("\n�����Ŀ��������յ�����Ϣ\n");
	printf("%s", Buffer);
	printf("\n----------------------------------------------\n");
	if (!memcmp(&Buffer[9], "304", 3)) {//������304
		printf("-----------------------���ػ���----------------------\n");
		ret = send(((ProxyParam*)lpParameter)->clientSocket, cache[i].buffer, sizeof(cache[i].buffer), 0);
		if (ret != SOCKET_ERROR) {
			printf("���淵�سɹ�\n");
		}
	}

	else {
		if (!strcmp(httpHeader->method, "GET") && !memcmp(&Buffer[9], "200", 3)) {//������200
			char Buffer2[MAXSIZE];
			memcpy(Buffer2, Buffer, sizeof(Buffer));
			const char* delim = "\r\n";
			char* ptr;
			char* p = strtok_s(Buffer2, delim, &ptr);//�ָ��ַ���
			bool flag = false;
			while (p) {
				if (strlen(p) >= 15 && !memcmp(p, "Last-Modified: ", 15)) {
					flag = true;
					break;
				}
				p = strtok_s(NULL, delim, &ptr);
			}
			//��ӻ���
			if (flag) {
				printf("��ӻ���\n");
				last_cache++;
				last_cache %= CACHE_NUM;
				memcpy(cache[last_cache].url, httpHeader->url, sizeof(httpHeader->url));
				memcpy(cache[last_cache].time, p + 15, strlen(p) - 15);
				memcpy(cache[last_cache].buffer, Buffer, sizeof(Buffer));
				printf("\n��ӵĻ���\n");
				printf("%s\n", cache[last_cache].url);
				printf("%s\n", cache[last_cache].time);
				printf("%s", Buffer);
				printf("\n----------------------------------------------\n");
			}
			//��Ŀ����������ص�����ֱ��ת�����ͻ���
		}
		printf("\n������������û���������\n");
		printf("%s", Buffer);
		printf("\n----------------------------------------------\n");
		ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
	}
	//������
error:
	printf("�ر��׽���\n");
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
// Qualifier: ���� TCP �����е� HTTP ͷ��
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char* buffer, HttpHeader* httpHeader) {
	char* p;
	char* ptr = NULL;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//��ȡ��һ��
	printf("%s\n", p);
	if (p[0] == 'G') {//GET ��ʽ
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0] == 'P') {//POST ��ʽ
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
// Qualifier: ������������Ŀ��������׽��֣�������
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
