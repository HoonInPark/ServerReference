#pragma once
#include "mysql.h"
#include <string>

using namespace std;

//�Ӽ� > ��Ŀ > �Է� > �߰����Ӽ����� ����� �����ָ� �Ʒ�ó�� �ڵ�� �߰��ص� �ȴ�.
#pragma comment(lib, "libmysql.lib")

class DBConnector
{
public:
	DBConnector();
	~DBConnector();

	// DB �� ����
	bool Connect(
		const string&	Server,
		const string&	User,
		const string&	Password,
		const string&	Database,
		const int&		Port
	);
	// DB ���� ����
	void Close();
	
	// ���� ������ ã��
	bool SearchAccount(const string& Id, const string& Password);
	// ���� ������ ���
	bool SignUpAccount(const string& Id, const string& Password);

private:
	MYSQL * Conn;		// Ŀ����
	MYSQL_RES * Res;	// �����
	MYSQL_ROW Row;		// ��� row
};

