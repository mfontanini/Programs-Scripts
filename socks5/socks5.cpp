//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//      
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//      
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
//  MA 02110-1301, USA.
//
//  Author: Matías Fontanini
//  Contact: matias.fontanini@gmail.com


#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <pthread.h>

#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <algorithm>
#include <set>


#define SERVER_PORT 45321
#define MAXPENDING 200
#define BUF_SIZE 256
#define USERNAME "username"
#define PASSWORD "password"


using namespace std;


/* Command constants */
#define CMD_CONNECT         1
#define CMD_BIND            2
#define CMD_UDP_ASSOCIATIVE 3

/* Address type constants */
#define ATYP_IPV4   1
#define ATYP_DNAME  3
#define ATYP_IPV6   4

/* Connection methods */
#define METHOD_NOAUTH       0
#define METHOD_AUTH         2
#define METHOD_NOTAVAILABLE 0xff

/* Responses */
#define RESP_SUCCEDED       0
#define RESP_GEN_ERROR      1


/* Handshake */

struct MethodIdentificationPacket {
    uint8_t version, nmethods;
    /* uint8_t methods[nmethods]; */
} __attribute__((packed));

struct MethodSelectionPacket {
    uint8_t version, method;
    MethodSelectionPacket(uint8_t met) : version(5), method(met) {}
} __attribute__((packed));


/* Requests */

struct SOCKS5RequestHeader {
    uint8_t version, cmd, rsv /* = 0x00 */, atyp;
} __attribute__((packed));

struct SOCK5IP4RequestBody {
    uint32_t ip_dst;
    uint16_t port;
} __attribute__((packed));

struct SOCK5DNameRequestBody {
    uint8_t length;
    /* uint8_t dname[length]; */
} __attribute__((packed));


/* Responses */

struct SOCKS5Response {
    uint8_t version, cmd, rsv /* = 0x00 */, atyp;
    uint32_t ip_src;
    uint16_t port_src;
    
    SOCKS5Response(bool succeded = true) : version(5), cmd(succeded ? RESP_SUCCEDED : RESP_GEN_ERROR), rsv(0), atyp(ATYP_IPV4) { }
} __attribute__((packed));


class Lock {
	pthread_mutex_t mutex;
public:
	Lock() {
        pthread_mutex_init(&mutex, NULL);
    }
  
    ~Lock() {
        pthread_mutex_destroy(&mutex);
    }
    
	inline void lock() {
        pthread_mutex_lock(&mutex);
    }
    
	inline void unlock() {
        pthread_mutex_unlock(&mutex);
    }
};

class Event {
	pthread_mutex_t mutex;
	pthread_cond_t condition;
public:
	Event()  { 
        pthread_mutex_init(&mutex, 0);
        pthread_cond_init(&condition, 0);
    }
    
	~Event() {
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&condition);
    }
    
	inline void lock() {
        pthread_mutex_lock(&mutex);
    }
    
	inline void unlock() {
        pthread_mutex_unlock(&mutex);
    }
    
	inline void signal() {
        pthread_cond_signal(&condition);
    }
    
	inline void broadcastSignal() {
        pthread_cond_broadcast(&condition);
    }
    
	inline void wait(){
        pthread_cond_wait(&condition, &mutex);
    }
};


Lock get_host_lock;
Event client_lock;
uint32_t client_count = 0, max_clients = 10;

void sig_handler(int signum) {
    
}

int create_listen_socket(struct sockaddr_in &echoclient) {
    int serversock;
    struct sockaddr_in echoserver;
    /* Create the TCP socket */
    if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        cout << "[-] Could not create socket.\n";
        return -1;
    }
    /* Construct the server sockaddr_in structure */
    memset(&echoserver, 0, sizeof(echoserver));       /* Clear struct */
    echoserver.sin_family = AF_INET;                  /* Internet/IP */
    echoserver.sin_addr.s_addr = htonl(INADDR_ANY);   /* Incoming addr */
    echoserver.sin_port = htons(SERVER_PORT);       /* server port */
    /* Bind the server socket */
    if (bind(serversock, (struct sockaddr *) &echoserver, sizeof(echoserver)) < 0) {
        cout << "[-] Bind error.\n";
        return -1;
    }
    /* Listen on the server socket */
    if (listen(serversock, MAXPENDING) < 0) {
        cout << "[-] Listen error.\n";
        return -1;
    }
    return serversock;
}

int recv_sock(int sock, char *buffer, uint32_t size) {
	int index = 0, ret;
	while(size) {
		if((ret = recv(sock, &buffer[index], size, 0)) <= 0)
			return (!ret) ? index : -1;
		index += ret;
		size -= ret;
	}
	return index;
}

int send_sock(int sock, const char *buffer, uint32_t size) {
	int index = 0, ret;
	while(size) {
		if((ret = send(sock, &buffer[index], size, 0)) <= 0)
			return (!ret) ? index : -1;
		index += ret;
		size -= ret;
	}
	return index;
}

string int_to_str(uint32_t ip) {
    ostringstream oss;
    for (unsigned i=0; i<4; i++) {
        oss << ((ip >> (i*8) ) & 0xFF);
        if(i != 3)
            oss << '.';
    }
    return oss.str();
}

int connect_to_host(uint32_t ip, uint16_t port) {
    struct sockaddr_in serv_addr;
    struct hostent *server;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        return -1;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; 
    string ip_string = int_to_str(ip);
    
    get_host_lock.lock();
    server = gethostbyname(ip_string.c_str());
    if(!server) {
        get_host_lock.unlock();
        return -1;
    }
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    get_host_lock.unlock();
    
    serv_addr.sin_port = htons(port);
    return !connect(sockfd, (const sockaddr*)&serv_addr, sizeof(serv_addr)) ? sockfd : -1;
}

int read_variable_string(int sock, uint8_t *buffer, uint8_t max_sz) {
    if(recv_sock(sock, (char*)buffer, 1) != 1 || buffer[0] > max_sz)
        return false;
    uint8_t sz = buffer[0];
    if(recv_sock(sock, (char*)buffer, sz) != sz)
        return -1;
    return sz;
}

bool check_auth(int sock) {
    uint8_t buffer[128];
    if(recv_sock(sock, (char*)buffer, 1) != 1 || buffer[0] != 1)
        return false;
    int sz = read_variable_string(sock, buffer, 127);
    if(sz == -1)
        return false;
    buffer[sz] = 0;
    if(strcmp((char*)buffer, USERNAME))
        return false;
    sz = read_variable_string(sock, buffer, 127);
    if(sz == -1)
        return false;
    buffer[sz] = 0;
    if(strcmp((char*)buffer, PASSWORD))
        return false;
    buffer[0] = 1;
    buffer[1] = 0;
    return send_sock(sock, (const char*)buffer, 2) == 2;
}

bool handle_handshake(int sock, char *buffer) {
    MethodIdentificationPacket packet;
    int read_size = recv_sock(sock, (char*)&packet, sizeof(MethodIdentificationPacket));
    if(read_size != sizeof(MethodIdentificationPacket) || packet.version != 5)
        return false;
    if(recv_sock(sock, buffer, packet.nmethods) != packet.nmethods)
        return false;
    MethodSelectionPacket response(METHOD_NOTAVAILABLE);
    for(unsigned i(0); i < packet.nmethods; ++i) {
        #ifdef ALLOW_NO_AUTH
            if(buffer[i] == METHOD_NOAUTH)
                response.method = METHOD_NOAUTH;
        #endif
        if(buffer[i] == METHOD_AUTH)
            response.method = METHOD_AUTH;
    }
    if(send_sock(sock, (const char*)&response, sizeof(MethodSelectionPacket)) != sizeof(MethodSelectionPacket) || response.method == METHOD_NOTAVAILABLE)
        return false;
    return (response.method == METHOD_AUTH) ? check_auth(sock) : true;
}

void set_fds(int sock1, int sock2, fd_set *fds) {
    FD_ZERO (fds);
    FD_SET (sock1, fds); 
    FD_SET (sock2, fds); 
}

void do_proxy(int client, int conn, char *buffer) {
    fd_set readfds; 
    int result, nfds = max(client, conn)+1;
    set_fds(client, conn, &readfds);
    while((result = select(nfds, &readfds, 0, 0, 0)) > 0) {
        if (FD_ISSET (client, &readfds)) {
            int recvd = recv(client, buffer, 256, 0);
            if(recvd <= 0)
                return;
            send_sock(conn, buffer, recvd);
        }
        if (FD_ISSET (conn, &readfds)) {
            int recvd = recv(conn, buffer, 256, 0);
            if(recvd <= 0)
                return;
            send_sock(client, buffer, recvd);
        }
        set_fds(client, conn, &readfds);
    }
}

bool handle_request(int sock, char *buffer) {
    SOCKS5RequestHeader header;
    recv_sock(sock, (char*)&header, sizeof(SOCKS5RequestHeader));
    if(header.version != 5 || header.cmd != CMD_CONNECT || header.rsv != 0)
        return false;
    int client_sock = -1;
    switch(header.atyp) {
        case ATYP_IPV4:
        {
            SOCK5IP4RequestBody req;
            if(recv_sock(sock, (char*)&req, sizeof(SOCK5IP4RequestBody)) != sizeof(SOCK5IP4RequestBody))
                return false;
            client_sock = connect_to_host(req.ip_dst, ntohs(req.port));
            break;
        }
        case ATYP_DNAME:
            break;
        default:
            return false;
    }
    if(client_sock == -1)
        return false;
    SOCKS5Response response;
    response.ip_src = 0;
    response.port_src = SERVER_PORT;
    send_sock(sock, (const char*)&response, sizeof(SOCKS5Response));
    do_proxy(client_sock, sock, buffer);
    shutdown(client_sock, SHUT_RDWR);
    close(client_sock);
    return true;
}

void *handle_connection(void *arg) {
    int sock = (uint64_t)arg;
    char *buffer = new char[BUF_SIZE];
    if(handle_handshake(sock, buffer))
        handle_request(sock, buffer);
    shutdown(sock, SHUT_RDWR);
    close(sock);
    delete[] buffer;
    client_lock.lock();
    client_count--;
    if(client_count == max_clients - 1)
        client_lock.signal();
    client_lock.unlock();
    return 0;
}

bool spawn_thread(pthread_t *thread, void *data) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    return !pthread_create(thread, &attr, handle_connection, data);
}

void parse_args(int argc, char *argv[]) {
    if(argc == 2)
        max_clients = atoi(argv[1]);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in echoclient;
    int listen_sock = create_listen_socket(echoclient);
    if(listen_sock == -1) {
        cout << "[-] Failed to create server\n";
        return 1;
    }
    parse_args(argc, argv);
    signal(SIGPIPE, sig_handler);
    while(true) {
        uint32_t clientlen = sizeof(echoclient);
        int clientsock;
        client_lock.lock();
        if(client_count == max_clients)
            client_lock.wait();
        client_lock.unlock();
        if ((clientsock = accept(listen_sock, (struct sockaddr *) &echoclient, &clientlen)) > 0) {
            client_lock.lock();
            client_count++;
            client_lock.unlock();
            pthread_t thread;
            spawn_thread(&thread, (void*)clientsock);
        }
    }
}

