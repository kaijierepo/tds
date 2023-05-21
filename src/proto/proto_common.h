#pragma once

#pragma pack(1)
enum class IO_PKT_TYPE {
	IO_Unknown,
	IO_Request,
	IO_Response,
	IO_Notify
};

class DEV_PKT {
public:
	unsigned char* data;
	size_t len;
	int transactionId;
	virtual bool pack() { return false; };
	virtual bool unpack() { return false; };
	void clear() {
		if (data != nullptr)
		{
			delete data;
			data = nullptr;
		}
		len = 0;
	}

	unsigned char* cmd_data;
	size_t cmd_data_len;
	string proto;
	IO_PKT_TYPE type;

	string m_strCmdName; //命令名称
	string m_strCmdContent;  //命令内容概要
	string m_strPktDetail; //命令包详细解析信息

	void* pushData(string s);
	void* pushData(void* p, int l);
	unsigned char* pushData(unsigned char v);
	unsigned short* pushData(unsigned short v);
	virtual bool pack(char* cmdData, int len) { return false; };

	virtual bool unpack(unsigned char* p, int len, bool withDetail = false) { return 0; };

	virtual string GetCmdID() { return ""; };
	virtual bool UnPack(LPVOID pBuf, int iBufLen, bool bGetCmdInfo = false) { return true; };
	virtual string GetPktDesc() { return ""; }; //包详细描述信息
	virtual string GetCmdName() { return ""; };

	void setData(unsigned char* p, size_t l)
	{
		if (data)delete data;
		data = new unsigned char[l];
		memcpy(data, p, l);
		len = l;
	}
	DEV_PKT(unsigned char* p, size_t l)
	{
		data = nullptr;
		setData(p, l);
	}
	DEV_PKT()
	{
		data = nullptr;
		len = 0;
		cmd_data = nullptr;
		cmd_data_len = 0;
		type = IO_PKT_TYPE::IO_Unknown;
		transactionId = -1;
	}

	~DEV_PKT()
	{
		if (data)
			delete data;
		if (cmd_data)
			delete cmd_data;
	}

	void copy(const DEV_PKT& r) {
		this->len = r.len;
		this->cmd_data_len = r.cmd_data_len;
		this->proto = r.proto;
		this->type = r.type;
		this->m_strCmdName = r.m_strCmdName;
		this->m_strCmdContent = r.m_strCmdContent;
		this->m_strPktDetail = r.m_strPktDetail;

		if (r.len > 0)
		{
			if (this->data)
				delete this->data;
			this->data = new unsigned char[r.len];
			memcpy(this->data, r.data, r.len);
		}
		else
		{
			this->data = nullptr;
		}

		if (r.cmd_data_len > 0)
		{
			if (this->cmd_data)delete this->cmd_data;
			this->cmd_data = new unsigned char[r.cmd_data_len];
			memcpy(cmd_data, r.cmd_data, r.cmd_data_len);
		}
		else
		{
			this->cmd_data = nullptr;
		}
	}

	DEV_PKT(const DEV_PKT& r)
	{
		copy(r);
	}


	DEV_PKT& operator=(const DEV_PKT& pd)
	{
		copy(pd);
		return *this;
	}
};
#pragma pack()