#pragma once
#include "httplib.h"
#include "common.h"

// tds client stream for itergration with httplib
//using a selfdefined tcpserver layer
//using httplib only as a http layer
// httplib.h is modified a little , easily keep update with files on github

// change process_request  function from protected to public

//从github更新新版本的httplib需要修改处
//1.中文路径支持。 tds使用utf8编码，httplib内部文件读取使用gb2312编码
//2.设置不缓存，目前tds的应用场景都不需要缓存
/*
if (!res.has_header("cache-control")) {
	res.set_header("cache-control", "max-age=1");
}*/
//3.process_request改为public

namespace httplib {
	namespace detail {
		class dsClientStream : public httplib::Stream {
		public:
			dsClientStream() = default;
			~dsClientStream(){
				clear();
			};

			bool haveData();

			bool is_readable() const override;
			bool is_writable() const override;
			ssize_t read(char* ptr, size_t size) override;
			ssize_t write(const char* ptr, size_t size) override;
			void get_remote_ip_and_port(std::string& ip, int& port) const override;
			const std::string& get_buffer() const;
			socket_t socket() const override;

			mutex m_cs;
			semaphore m_sem;

			void clear() {

			}

			struct BUFF {
				char* p;
				int len;
			};

			vector<BUFF> bufferList;

			void appendBuffer(char* data, int iLen)
			{
				m_cs.lock();
				BUFF bf;
				bf.p = new char[iLen];
				bf.len = iLen;
				memcpy(bf.p, data, iLen);
				bufferList.push_back(bf);
				m_cs.unlock();
				m_sem.notify();
			}

			socket_t sock_;
		};

		bool dsClientStream::haveData() {
			bool haveData = false;
			m_cs.lock();
			if (bufferList.size() > 0)
			{
				haveData = true;
			}
			m_cs.unlock();
			return haveData;
		}

		// dsClientStream stream implementation
		bool dsClientStream::is_readable() const { 
			return true;
		}

		bool dsClientStream::is_writable() const { return true; }

		ssize_t dsClientStream::read(char* ptr, size_t size)
		 {
			 int len_read = 0;

			 while (1)
			 {
				 if (len_read > 0)
				 {
					 break;
				 }
				 else
				 {
					 bool bEmpty = false;
					 m_cs.lock();
					 if (bufferList.size() == 0)
					 {
						 bEmpty = true;
					 }
					 m_cs.unlock();
					 if (bEmpty)
					 {
						 if (!m_sem.wait_for(3000))
						 {
							 return 0;
						 }
					 } 	 
				 }


				 std::unique_lock<mutex> lock(m_cs);
				 if (bufferList.size() == 0)
				 {
					 continue;
				 }
				 BUFF& bf = bufferList.at(0);
				 len_read = bf.len < size ? bf.len : size;
				 memcpy(ptr, bf.p, len_read);

				 if (bf.len - len_read > 0)
				 {
					 bf.len -= len_read;
					 char* pOld = bf.p;
					 bf.p = new char[bf.len];
					 memcpy(bf.p, pOld + len_read, bf.len);
					 delete pOld;
				 }
				 else
				 {
					 delete bf.p;
					 bufferList.erase(bufferList.begin());
				 }
			 }

			 return static_cast<ssize_t>(len_read);
		 }

		ssize_t dsClientStream::write(const char* ptr, size_t size) {
			if (is_writable()) { 
				size_t sended = send(sock_, ptr, size, 0);
				return sended;
			}
			return -1;
		}

		void dsClientStream::get_remote_ip_and_port(std::string& ip, int& port) const {  }

		const std::string& dsClientStream::get_buffer() const { 
			return nullptr; 
		}

		socket_t dsClientStream::socket() const { return 0; }
	}
}

