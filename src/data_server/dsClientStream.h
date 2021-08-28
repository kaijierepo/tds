#pragma once
#include "httplib.h"


// tds client stream for itergration with httplib
//using a selfdefined tcpserver layer
//using httplib only as a http layer
// httplib.h is modified a little , easily keep update with files on github

// change process_request  function from protected to public

namespace httplib {
	namespace detail {
		class dsClientStream : public httplib::Stream {
		public:
			dsClientStream() = default;
			~dsClientStream(){
				clear();
			};

			bool is_readable() const override;
			bool is_writable() const override;
			ssize_t read(char* ptr, size_t size) override;
			ssize_t write(const char* ptr, size_t size) override;
			void get_remote_ip_and_port(std::string& ip, int& port) const override;
			const std::string& get_buffer() const;
			socket_t socket() const override;

			void clear() {
				if (buffer != nullptr)delete buffer;
				buffer = nullptr;
				buffSize = 0;
			}

			void appendBuffer(char* data, int iLen)
			{
				if (buffer == nullptr)
				{
					buffer = new char[iLen];
				}
				else
				{
					char* pOld = buffer;
					buffer = new char[buffSize + iLen];
					memcpy(buffer, pOld, buffSize);
					delete pOld;
				}
				memcpy(buffer + buffSize, data, iLen);
				buffSize += iLen;
			}

			char* buffer = nullptr;
			size_t buffSize = 0;
			socket_t sock_;
		};

		// dsClientStream stream implementation
		inline bool dsClientStream::is_readable() const { return true; }

		inline bool dsClientStream::is_writable() const { return true; }

		inline ssize_t dsClientStream::read(char* ptr, size_t size) {
			auto len_read = buffSize < size ? buffSize  : size;
			memcpy(ptr, buffer, len_read);
			buffSize -= len_read;
			if (buffSize == 0)
			{
				delete buffer;
				buffer = nullptr;
			}
			else
			{
				char* pOld = buffer;
				buffer = new char[buffSize];
				memcpy(buffer,pOld + len_read, buffSize);
				delete pOld;
			}
			return static_cast<ssize_t>(len_read);
		}

		inline ssize_t dsClientStream::write(const char* ptr, size_t size) {
			if (is_writable()) { return send(sock_, ptr, size, 0); }
			return -1;
		}

		inline void dsClientStream::get_remote_ip_and_port(std::string& ip, int& port) const {  }

		inline const std::string& dsClientStream::get_buffer() const { return buffer; }

		inline socket_t dsClientStream::socket() const { return 0; }
	}
}

