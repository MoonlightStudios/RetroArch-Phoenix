#ifndef NALL_HTTP_HPP
#define NALL_HTTP_HPP

#if !defined(_WIN32)
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
#else
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
#endif

#include <nall/platform.hpp>
#include <nall/string.hpp>
#include <nall/function.hpp>

namespace nall {

struct http {
  string hostname;
  addrinfo *serverinfo;
  int serversocket;
  string header;

  unsigned total_length;
  function<bool (unsigned, unsigned)> progress_cb;

  inline void set_progress_cb(const function<bool (unsigned, unsigned)> &cb) {
    progress_cb = cb;
  }

  inline bool download(const string &path, uint8_t *&data, unsigned &size) {
    data = 0;
    size = 0;
    total_length = 0;

    send({
      "GET ", path, " HTTP/1.1\r\n"
      "Host: ", hostname, "\r\n"
      "Connection: close\r\n"
      "User-Agent: nall::http\r\n"
      "\r\n"
    });

    header = downloadHeader();

    if (!header.iposition("200 OK"))
       return false;

    if (header.length() == 0)
       return false;

    return downloadContent(data, size);
  }

  inline bool connect(string host, unsigned port = 80) {
    hostname = host;

    addrinfo hints;
    memset(&hints, 0, sizeof(addrinfo));

#ifdef _WIN32
    hints.ai_family = AF_INET;
#else
    hints.ai_family = AF_UNSPEC;
#endif
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(hostname, string(port), &hints, &serverinfo);
    if(status != 0) return false;

    serversocket = socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol);
    if(serversocket == -1) return false;

    int result = ::connect(serversocket, serverinfo->ai_addr, serverinfo->ai_addrlen);
    if(result == -1) return false;

    return true;
  }

  inline bool send(const string &data) {
    return send((const uint8_t*)(const char*)data, data.length());
  }

  inline bool send(const uint8_t *data, unsigned size) {
    while(size) {
      int length = ::send(serversocket, data, size, 0);
      if(length == -1) return false;
      data += length;
      size -= length;
    }
    return true;
  }

  inline string downloadHeader() {
    string output;
    do {
      char buffer[2];
      int length = recv(serversocket, buffer, 1, 0);
      if(length <= 0) return output;
      buffer[1] = 0;
      output.append(buffer);
    } while(output.endswith("\r\n\r\n") == false);
    return output;
  }

  inline string downloadChunkLength() {
    string output;
    do {
      char buffer[2];
      int length = recv(serversocket, buffer, 1, 0);
      if(length <= 0) return output;
      buffer[1] = 0;
      output.append(buffer);
    } while(output.endswith("\r\n") == false);
    return output;
  }

  inline bool downloadContent(uint8_t *&data, unsigned &size) {
    unsigned capacity = 0;

    if(header.iposition("\r\nTransfer-Encoding: chunked\r\n")) {
      while(true) {
        unsigned length = hex(downloadChunkLength());
        if(length == 0) break;
        capacity += length;
        data = (uint8_t*)realloc(data, capacity);

        char buffer[length];
        while(length) {
          int packetlength = recv(serversocket, buffer, length, 0);
          if(packetlength <= 0) break;
          memcpy(data + size, buffer, packetlength);
          size += packetlength;
          length -= packetlength;
        }

        if(progress_cb && !progress_cb(capacity, total_length))
          return false;
      }
    } else if(auto position = header.iposition("\r\nContent-Length: ")) {
      unsigned length = decimal((const char*)header + position() + 18);
      total_length = length;
      while(length) {
        char buffer[2048];
        int packetlength = recv(serversocket, buffer, min(sizeof(buffer), length), 0);
        if(packetlength <= 0) break;
        capacity += packetlength;
        data = (uint8_t*)realloc(data, capacity);
        memcpy(data + size, buffer, packetlength);
        size += packetlength;
        length -= packetlength;

        if(progress_cb && !progress_cb(capacity, total_length))
          return false;
      }

      if(capacity < total_length)
        return false;
    } else {
      while(true) {
        char buffer[2048];
        int packetlength = recv(serversocket, buffer, 2048, 0);
        if(packetlength <= 0) break;
        capacity += packetlength;
        data = (uint8_t*)realloc(data, capacity);
        memcpy(data + size, buffer, packetlength);
        size += packetlength;

        if(progress_cb && !progress_cb(capacity, total_length))
          return false;
      }
    }

    data = (uint8_t*)realloc(data, capacity + 1);
    data[capacity] = 0;
    return true;
  }

  inline void disconnect() {
    close(serversocket);
    freeaddrinfo(serverinfo);
    serverinfo = 0;
    serversocket = -1;
  }

  #ifdef _WIN32
  inline int close(int sock) {
    return closesocket(sock);
  }

  inline http() {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock == INVALID_SOCKET && WSAGetLastError() == WSANOTINITIALISED) {
      WSADATA wsaData;
      if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        WSACleanup();
        return;
      }
    } else {
      close(sock);
    }
  }
  #endif
};

}

#endif
