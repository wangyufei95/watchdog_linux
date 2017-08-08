#pragma once
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cstdio>
using namespace std;
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <string>
#include <fstream>

#define FAILED					0x00000001
#define YES						0x00000002
#define NO						0x00000003

#define PARAM_INCORRECT			0x00000010
#define CREATE_SOCKET_FAILED	0x00000012
#define CONNECT_SERVER_FAILED	0x00000013
#define CHANGE_URL_TO_IP_FAILED	0x00000014

class CAutoMail
{
public:
	CAutoMail();
	virtual ~CAutoMail();

public:
	bool init();
	void uninit();
	bool send_mail();

private:
	bool init_socket();
	void init_mail_info();
	bool base64(wstring data, const char *dst_file);
	void get_time(char *time);
	void send_mail_head(ofstream &ofs);
	void send_mail_data(ofstream &ofs);
	
public:
	unsigned int get_error_no();

private:
	wstring m_str_src_mail_p; // sender in plaintext
	wstring m_str_src_mail_c; // sender in ciphertext
	
	wstring m_str_dst_mail_p; // receiver in plaintext
	wstring m_str_dst_mail_c; // receiver in ciphertext
	
	wstring m_str_pwd_p;	// password in plaintext
	wstring m_str_pwd_c;	// password in ciphertext

	wstring m_str_title; // title of email
	wstring m_str_data; // content of email

	ofstream m_ofs_log;

	wstring m_str_email_server;
	unsigned short m_ns_server_port;
private:
	int m_socket_server;
	struct sockaddr_in m_sockaddr_server;
private:
	const static wchar_t *WSTR_DEFAULT_TITLE;
	const static char *STR_TMP_FILE;
	const static char *STR_BASE_FILE;
	static unsigned int ERROR_NUMBER;
	const static int BUF_LEN;
	const static char *STR_LOG_FILE;
	const static char *STR_EMAIL_CONF_FILE;
	const static char *STR_CODE;
};


const char *CAutoMail::STR_TMP_FILE = "base.tmp";
const char *CAutoMail::STR_BASE_FILE = "base.conf";
const char *CAutoMail::STR_LOG_FILE = "info.log";
const char *CAutoMail::STR_EMAIL_CONF_FILE = "email.conf";
unsigned int CAutoMail::ERROR_NUMBER = 0x00000001;
const int CAutoMail::BUF_LEN = 256;
const char *CAutoMail::STR_CODE = "UTF-8";

CAutoMail::CAutoMail() 
{
	/*
	 * set code for multi-char language, such as Chinese
	*/
	locale loc("zh_CN.UTF-8");
    locale::global( loc );
}

CAutoMail::~CAutoMail() {}

void CAutoMail::init_mail_info()
{
	wchar_t _buf[BUF_LEN] = {0};
	wifstream _ifs(STR_EMAIL_CONF_FILE);
	int _iPrefixLen = 4; // length of prefix per line, such as src=xxx@gmail.com, the prefix is "src="
	wstring _str_pre;
	
	while (!_ifs.eof()) 
	{
		memset(_buf, 0, sizeof(_buf));
		_ifs.getline(_buf, sizeof(_buf));
		_str_pre = _buf;
    	_str_pre = _str_pre.substr(0, _iPrefixLen); // get the prefix

		if (wcscmp(_str_pre.data(), L"src=") == 0) // get sender mail address
		{
			m_str_src_mail_p = _buf;
			m_str_src_mail_p = m_str_src_mail_p.substr(_iPrefixLen);
		}
		
		else if (wcscmp(_str_pre.data(), L"dst=") == 0) // get receiver mail address 
		{
			m_str_dst_mail_p = _buf;
			m_str_dst_mail_p = m_str_dst_mail_p.substr(_iPrefixLen);
		}
		else if (wcscmp(_str_pre.data(), L"svr=") == 0) // get the mail server, like "smtp.163.com"
		{
			m_str_email_server = _buf; 
			m_str_email_server = m_str_email_server.substr(_iPrefixLen);
		}
		else if (wcscmp(_str_pre.data(), L"prt=") == 0) // get the server port
		{
			char _buf_tmp[16] = {0};
			wstring _str(_buf);
			_str = _str.substr(_iPrefixLen);
			wcstombs(_buf_tmp, _str.data(), _iPrefixLen+1);
			m_ns_server_port = atoi(_buf_tmp);
		}
		else if (wcscmp(_str_pre.data(), L"psd=") == 0) // get sender email password
		{
			m_str_pwd_p = _buf;
			m_str_pwd_p = m_str_pwd_p.substr(_iPrefixLen);
		}
		else if (wcscmp(_str_pre.data(), L"dat=") == 0) // get the content of email
		{
			m_str_data = _buf;
			m_str_data = m_str_data.substr(_iPrefixLen);			
		}
		else if (wcscmp(_str_pre.data(), L"tit=") == 0) // get the title of email
		{
			m_str_title = _buf;
			m_str_title = m_str_title.substr(_iPrefixLen);
		}
		else // if content is multi-line，get all
		{
			m_str_data += L"\n";
			m_str_data += _buf;
		}
	}
	_ifs.close();
}

unsigned int CAutoMail::get_error_no()
{
	return ERROR_NUMBER;
}


bool CAutoMail::init()
{
		
	init_mail_info();

	if (!init_socket())
		return false;

	return true;
}

void CAutoMail::uninit()
{
	shutdown(m_socket_server, SHUT_RDWR);
	close(m_socket_server);
	char _buf[BUF_LEN] = {0};
	
	memset(_buf, 0, sizeof(_buf));
	sprintf(_buf, "rm -f %s", STR_BASE_FILE);
	system(_buf);
	
	memset(_buf, 0, sizeof(_buf));
	sprintf(_buf, "rm -f %s", STR_TMP_FILE);
	system(_buf);
}

bool CAutoMail::init_socket()
{
	char _buf_svr[BUF_LEN] = {0};
	wcstombs(_buf_svr, m_str_email_server.data(), m_str_email_server.length()+1);
	hostent *host_server = gethostbyname(_buf_svr);

	if (host_server == NULL)
	{
		ERROR_NUMBER = CHANGE_URL_TO_IP_FAILED;
		return false;
	}

	m_socket_server = socket(AF_INET,SOCK_STREAM,0);
	if (m_socket_server < 0)
	{
		ERROR_NUMBER = CREATE_SOCKET_FAILED;
		return false;
	}

	m_sockaddr_server.sin_family = AF_INET;
 	m_sockaddr_server.sin_port = htons(m_ns_server_port);
	m_sockaddr_server.sin_addr = *((struct in_addr *)host_server->h_addr);
	memset(&(m_sockaddr_server.sin_zero), 0, 8) ;

	return true;
}

bool CAutoMail::base64(wstring data, const char *dst_file)
{
	if (dst_file == NULL)
	{
		ERROR_NUMBER = PARAM_INCORRECT;
		return false;
	}

	wofstream _ofs(STR_TMP_FILE); // create temp file to store data
	_ofs << data.data();
	_ofs.close();
	
	char buf[BUF_LEN] = {0}; // to encode data with base64 by shell
	sprintf(buf,"base64 %s >> %s", STR_TMP_FILE, dst_file);
	system(buf);

	memset(buf, 0, sizeof(buf));
	sprintf(buf, "rm -f %s", STR_TMP_FILE);
	system(buf);

	return true;
}

void CAutoMail::get_time(char *time_buf)
{
	if (time_buf == NULL)
		return;
	time_t tt = time(NULL);
	struct tm *time_now = localtime(&tt);
	sprintf(time_buf, "%d-%d-%d-%d:%d:%d", time_now->tm_year+1900, time_now->tm_mon+1, time_now->tm_mday, time_now->tm_hour, 
	        time_now->tm_min, time_now->tm_sec);
}

void CAutoMail::send_mail_head(ofstream &ofs)
{
	char buf_send[BUF_LEN] = {0};
	char buf_recv[BUF_LEN] = {0};
	char time[BUF_LEN] = {0};
	int reval = 0;

	sprintf(buf_send, "EHLO %S\r\n", m_str_src_mail_p.data());
	send(m_socket_server, buf_send, strlen(buf_send), 0); // send with src mail
	recv(m_socket_server, buf_recv, sizeof(buf_recv), 0);
	get_time(time);
	ofs << time << ": " << buf_recv;

	memset(buf_send, 0, sizeof(buf_send));
	memset(buf_recv, 0, sizeof(buf_recv));
	sprintf(buf_send,"AUTH LOGIN\r\n");
	send(m_socket_server, buf_send, strlen(buf_send), 0);
	recv(m_socket_server, buf_recv, sizeof(buf_recv), 0);
	get_time(time);
	ofs << time << ": " << buf_recv;

	memset(buf_send, 0, sizeof(buf_send));
	memset(buf_recv, 0, sizeof(buf_recv));
	sprintf(buf_send, "%S\r\n", m_str_src_mail_c.data());
	send(m_socket_server, buf_send, strlen(buf_send), 0); // send username, after encode with base64
	recv(m_socket_server, buf_recv, sizeof(buf_recv), 0);
	get_time(time);
	ofs << time << ": " << buf_recv;

	memset(buf_send, 0, sizeof(buf_send));
	memset(buf_recv, 0, sizeof(buf_recv));
	sprintf(buf_send, "%S\r\n", m_str_pwd_c.data());
	send(m_socket_server, buf_send, strlen(buf_send), 0); // send password, after encode with base64
	recv(m_socket_server, buf_recv, sizeof(buf_recv), 0);
	get_time(time);
	ofs << time << ": " << buf_recv;
}

void CAutoMail::send_mail_data(ofstream &ofs)
{
	char buf_send[BUF_LEN] = {0};
	char buf_recv[BUF_LEN] = {0};
	char buf_time[BUF_LEN] = {0};
	int reval = 0;

	sprintf(buf_send, "MAIL FROM: <%S>\r\n", m_str_src_mail_p.data());
	send(m_socket_server, buf_send, strlen(buf_send), 0);
	recv(m_socket_server, buf_recv, sizeof(buf_recv), 0);
	get_time(buf_time);
	ofs << buf_time << ": " << buf_recv;

	memset(buf_recv, 0, sizeof(buf_recv));
	memset(buf_time, 0, sizeof(buf_time));
	memset(buf_send, 0, sizeof(buf_send));
	sprintf(buf_send, "RCPT TO: <%S>\r\n", m_str_dst_mail_p.data());
	send(m_socket_server, buf_send, strlen(buf_send), 0);
	recv(m_socket_server, buf_recv, sizeof(buf_recv), 0);
	get_time(buf_time);
	ofs << buf_time << ": " << buf_recv;

	memset(buf_recv, 0, sizeof(buf_recv));
	memset(buf_time, 0, sizeof(buf_time));
	memset(buf_send, 0, sizeof(buf_send));
	sprintf(buf_send, "DATA\r\n");
	send(m_socket_server, buf_send, strlen(buf_send), 0);
	recv(m_socket_server, buf_recv, sizeof(buf_recv), 0);
	get_time(buf_time);
	ofs << buf_time << ": " << buf_recv;

	memset(buf_send, 0, sizeof(buf_send));

	sprintf(buf_send, "FROM: %S\r\nTO: %S\r\n", m_str_src_mail_p.data(), m_str_dst_mail_p.data());
	send(m_socket_server, buf_send, strlen(buf_send), 0);
	
	memset(buf_send, 0, sizeof(buf_send));
	sprintf(buf_send, "SUBJECT: %S\r\n", m_str_title.data());
	send(m_socket_server, buf_send, strlen(buf_send), 0);

	memset(buf_send, 0, sizeof(buf_send));
	sprintf(buf_send, "Content-type: text/plain;charset=%s\r\n", STR_CODE);
	send(m_socket_server, buf_send, strlen(buf_send), 0);

	memset(buf_send, 0, sizeof(buf_send));
	sprintf(buf_send, "\r\n%S\r\n", m_str_data.data());
	send(m_socket_server, buf_send, strlen(buf_send), 0);

	memset(buf_send, 0, sizeof(buf_send));
	memset(buf_recv, 0, sizeof(buf_recv));
	sprintf(buf_send, "\r\n.\r\n");
	send(m_socket_server, buf_send, strlen(buf_send), 0);
	recv(m_socket_server, buf_recv, sizeof(buf_recv), 0);
	get_time(buf_time);
	ofs << buf_time << ": " << buf_recv;

	memset(buf_send, 0, sizeof(buf_send));
	memset(buf_recv, 0, sizeof(buf_recv));
	sprintf(buf_send, "QUIT\r\n");
	send(m_socket_server, buf_send, strlen(buf_send), 0);
	recv(m_socket_server, buf_recv, sizeof(buf_recv), 0);
	get_time(buf_time);
	ofs << buf_time << ": " << buf_recv;
}

bool CAutoMail::send_mail()
{
	m_str_src_mail_p;
	m_str_pwd_p;
	m_str_dst_mail_p;
	
	base64(m_str_src_mail_p, STR_BASE_FILE);
	base64(m_str_pwd_p, STR_BASE_FILE);
	base64(m_str_dst_mail_p, STR_BASE_FILE);
	

	char buf_send[BUF_LEN] = {0};
	char buf_recv[BUF_LEN] = {0};
	char buf_time[BUF_LEN] = {0};

	ofstream ofs(STR_LOG_FILE);
	wchar_t _buf_info[BUF_LEN] = {0};
	char buf[BUF_LEN] = {0};
	
	wifstream ifs(STR_BASE_FILE);
	ifs.getline(_buf_info,sizeof(_buf_info));
	m_str_src_mail_c = _buf_info;
	memset(_buf_info, 0, sizeof(_buf_info));
	ifs.getline(_buf_info,sizeof( _buf_info));
	m_str_pwd_c = _buf_info;
	memset(_buf_info, 0, sizeof( _buf_info));
	ifs.getline(_buf_info,sizeof(_buf_info));
	m_str_dst_mail_c = _buf_info;
	ifs.close();
	
	int reval = connect(m_socket_server, (struct sockaddr*)&m_sockaddr_server, sizeof(struct sockaddr));
	// connect to mail server
	if (reval != 0)
	{
		ERROR_NUMBER = CONNECT_SERVER_FAILED;
		uninit();
		return false;
	}

	recv(m_socket_server, buf, sizeof(buf), 0); // get welcome message from server
	memset (buf_time, 0, sizeof(buf_time));
	get_time(buf_time);
	ofs << buf_time << ": " << buf;
	
	send_mail_head(ofs);
	send_mail_data(ofs);
	
	ofs.close();
	cout << "send completed." << endl;
	return true;
}

int run_mail()
{
	CAutoMail automail;
	automail.init();
	if(!automail.send_mail())
		cout<<automail.get_error_no()<<endl;
	automail.uninit();
	return 0;
}
