#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"

/*
傲华尔远程控制协议
一、数据处理通信协议
  1、【读】数据，r指令：[版本,验证TOKEN,物云名,r指令,点1,点2,点3]
      请求：[2,"IQK","C1201020756","r","AI9","BO1"]
      返回：["C1201020756",["AI9",48.8,1540697466,0],["BO1",1,1540697466,0],"r"]
      返回：[物云名,[点1,值,时间戳,状态],[点2,值,时间戳,状态],r指令]
  2、【写】数据，w指令：[版本,验证TOKEN,物云名,w指令,[点1,值],[点2,值],[点3,值]]
      请求：[2,"IQK","C1201020756","w",["AO9",5],["BO4",1]]
      返回：["C1201020756",["AO9",5,1540697972,0],["BO4",1,1540697972,0],"w"]
      返回：[物云名,[点1,值,时间戳,状态],[点2,值,时间戳,状态],w指令]
  3、【监】数据，用控制器的内部SHELL命令[w n i点],把点加到监视通信列表中
      执行：[w 1 iAI9],把AI9加到主动发送数据列表中,当AI9数据有变化到COV值时会主动传送
      每经过设定变化时间后传送所有变化数据，传送的数据格式是【读】数据的格式少r指令
      注意：执行SHELL命令会切换到SHELL模式,通过发送以'['开头的信息切换到主动传送模式
二、对象处理通信协议
  1、【查】设备，hc指令：[版本,验证TOKEN,物云名,hc指令]
      请求：[2,"IQK","","hc"]
      返回：["C1201020756",{"Encoding":"UTF-8","Version":"IQ80 v2.0","SerialNumber":4294967295,
        "DisplayName":"物联网控制器","User":"HuangCong","DeviceID":1,"IP":"192.168.3.2"},"hc"]
      返回：[物云名,{键1:值,键2:值,键3:值},hc指令]
  2、【搜】对象，hs指令：[版本,验证TOKEN,物云名,hs指令]
      请求：[2,"IQK","C1201020756","hs"]
      返回：["C1201020756","AI9","AO8","BI1","RI3","FR1","FA2","BO1","RH496","hs"]
      返回：[物云名,点1,点2,点3,hs指令]]
  3、【读】对象，hr指令：[版本,验证TOKEN,物云名,hr指令,点1,点2,点3]
      请求：[2,"IQK","C1201020756","hr","BO1","AI9"]
      返回：["C1201020756",{"Name":"BO1","COV":1,"Enable":1,"ValueType":"bool","RW":"rw","Unit":"关:0,开:1"}
        ,{"Name":"AI9","DisplayName":"CPU温度","COV":0.5,"Enable":1,"ValueType":"float","RW":"ro","Unit":"℃"},"hr"]
      返回：[物云名,{键1:值,键2:值,键3:值},{键1:值,键2:值,键3:值},hr指令]
三、验证TOKEN处理通信协议
  1、【读】动态验证TOKEN，ht指令：[版本,验证TOKEN,物云名,ht指令]
      请求：[2,"IQK","C1201020756","ht"]
      返回：["C1201020756","vphpxlpxflbvjjlt","259200","ht"]
      返回：[物云名,动态验证TOKEN,有效时长(秒),ht指令] (有效时长为0时,永久有效)
  2、【写】动态验证TOKEN，ht指令：[版本,验证TOKEN,物云名,ht指令,动态验证TOKEN,有效时长(秒)]
      请求：[2,"IQK","C1201020756","ht","NewToken",7200]
      返回：["C1201020756","NewToken","7200","ht"]
      返回：[物云名,动态验证TOKEN,有效时长(秒),ht指令]
      建议：固定TOKEN仅用于读写动态TOKEN,其他操作用动态TOKEN
四、其他说明
  1、用SOCKET连接通信，连接IP地址、端口和角色通过ip指令配置查询
  2、通信格式为JSON格式，以换行符结尾
  3、通信数据帧长度小于2
*/



class ioDev_iq60 : public ioDev
{
public:
	ioDev_iq60();
	~ioDev_iq60();
	bool onRecvPkt(json jPkt);
	bool getCurrentVal();
	bool waitResponse(int timeout);
	bool sendData(char* pData, int iLen);
	bool requestAndWaitResp(string cmd, string req);
	bool scanChannel(json& chanList) override;
	bool output(string chanAddr, json jVal, json& jResp, bool sync=false) override;

	json getAddr() override;
	string currentCmd;
	vector<json> currentResp;//分包组包
	bool getResponse;
};

extern void onRecvIQ60Pkt(char* pData, int iLen,std::shared_ptr<TDS_SESSION> pALC);