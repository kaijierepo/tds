#include "pch.h"
#include "proto_common.h"
#include "common/common.h"

void* DEV_PKT::pushData(string s)
{
	vector<unsigned char> vb = str::toBytes(s);

	return pushData(vb.data(),vb.size());
}

void* DEV_PKT::pushData(void* p, int l) {
	size_t lastLen = len;
	unsigned char* pNew = new unsigned char[len + l];
	if (data != nullptr) {
		memcpy(pNew, data, len);
		delete data;
	}
	if (p != nullptr) {
		memcpy(pNew + len, p, l);
	}
	else {
		memset(pNew + len, 0, l);
	}

	len += l;
	data = pNew;
	return data + lastLen;
}

unsigned char* DEV_PKT::pushData(unsigned char v)
{
	return (unsigned char*)pushData(&v,1);
}

unsigned short* DEV_PKT::pushData(unsigned short v)
{
	return (unsigned short*)pushData(&v, 2);
}
