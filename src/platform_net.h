#ifndef FLXTR_PLATFORM_NET_H
#define FLXTR_PLATFORM_NET_H
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
typedef SOCKET FlxtrSocket;
typedef int FlxtrSocklen;
#define FLXTR_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int FlxtrSocket;
typedef socklen_t FlxtrSocklen;
#define FLXTR_INVALID_SOCKET (-1)
#endif
static inline int flxtr_socket_valid(FlxtrSocket socket_handle){return socket_handle!=FLXTR_INVALID_SOCKET;}
/* Each successful start is paired with stop by its owner. */
static inline int flxtr_network_start(void){
#ifdef _WIN32
    WSADATA data;int error=WSAStartup(MAKEWORD(2,2),&data);
    if(error){errno=EIO;return -1;}
#endif
    return 0;
}
static inline void flxtr_network_stop(void){
#ifdef _WIN32
    WSACleanup();
#endif
}
static inline void flxtr_socket_error(void){
#ifdef _WIN32
    int error=WSAGetLastError();
    errno=error==WSAEWOULDBLOCK?EAGAIN:error==WSAEINTR?EINTR:
          error==WSAECONNRESET?ECONNRESET:error==WSAECONNREFUSED?ECONNREFUSED:EIO;
#endif
}
static inline void flxtr_socket_close(FlxtrSocket socket_handle){
    if(!flxtr_socket_valid(socket_handle))return;
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}
static inline void flxtr_socket_shutdown_send(FlxtrSocket socket_handle){
#ifdef _WIN32
    shutdown(socket_handle,SD_SEND);
#else
    shutdown(socket_handle,SHUT_WR);
#endif
}
static inline int flxtr_socket_flags(FlxtrSocket socket_handle){
#ifdef _WIN32
    u_long mode=1;
    if(ioctlsocket(socket_handle,FIONBIO,&mode)||
       !SetHandleInformation((HANDLE)socket_handle,HANDLE_FLAG_INHERIT,0)){
        flxtr_socket_error();return -1;
    }
#else
    if(fcntl(socket_handle,F_SETFD,FD_CLOEXEC)<0||fcntl(socket_handle,F_SETFL,O_NONBLOCK)<0)return -1;
#ifdef SO_NOSIGPIPE
    int enabled=1;
    if(setsockopt(socket_handle,SOL_SOCKET,SO_NOSIGPIPE,&enabled,sizeof(enabled)))return -1;
#endif
#endif
    return 0;
}
static inline ptrdiff_t flxtr_socket_recv(FlxtrSocket socket_handle,void *bytes,size_t size){
    if(size>INT_MAX)size=INT_MAX;
#ifdef _WIN32
    int count=recv(socket_handle,(char *)bytes,(int)size,0);
#else
    ssize_t count=recv(socket_handle,bytes,size,0);
#endif
    if(count<0)flxtr_socket_error();
    return count;
}
static inline ptrdiff_t flxtr_socket_send(FlxtrSocket socket_handle,const void *bytes,size_t size){
    if(size>INT_MAX)size=INT_MAX;
#ifdef _WIN32
    int count=send(socket_handle,(const char *)bytes,(int)size,0);
#else
    int flags=0;
#ifdef MSG_NOSIGNAL
    flags=MSG_NOSIGNAL;
#endif
    ssize_t count=send(socket_handle,bytes,size,flags);
#endif
    if(count<0)flxtr_socket_error();
    return count;
}
static inline int flxtr_private_ipv4(uint32_t ip){
    return (ip>>24)==10||(ip>>20)==0xac1||(ip>>16)==0xc0a8;
}
static inline int flxtr_lan_ipv4(char *out,size_t size){
    if(!out||size<INET_ADDRSTRLEN){errno=EINVAL;return -1;}
#ifdef _WIN32
    ULONG bytes=0;
    ULONG flags=GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER;
    if(GetAdaptersAddresses(AF_INET,flags,NULL,NULL,&bytes)!=ERROR_BUFFER_OVERFLOW)return -1;
    IP_ADAPTER_ADDRESSES *list=(IP_ADAPTER_ADDRESSES *)malloc(bytes);
    if(!list){errno=ENOMEM;return -1;}
    ULONG error=GetAdaptersAddresses(AF_INET,flags,NULL,list,&bytes);
    if(error!=NO_ERROR){free(list);errno=EIO;return -1;}
    MIB_IPFORWARDROW best;memset(&best,0,sizeof(best));
    DWORD preferred=GetBestRoute(htonl(0x08080808),0,&best)==NO_ERROR?best.dwForwardIfIndex:0;
    int result=-1;
    for(IP_ADAPTER_ADDRESSES *p=list;p;p=p->Next){
        if(p->OperStatus!=IfOperStatusUp||p->IfType==IF_TYPE_SOFTWARE_LOOPBACK)continue;
        for(IP_ADAPTER_UNICAST_ADDRESS *u=p->FirstUnicastAddress;u;u=u->Next){
            if(!u->Address.lpSockaddr||u->Address.lpSockaddr->sa_family!=AF_INET)continue;
            struct sockaddr_in *a=(struct sockaddr_in *)u->Address.lpSockaddr;
            if(!flxtr_private_ipv4(ntohl(a->sin_addr.s_addr)))continue;
            if(result==0&&p->IfIndex!=preferred)continue;
            if(inet_ntop(AF_INET,&a->sin_addr,out,size)){result=0;if(p->IfIndex==preferred)break;}
        }
        if(result==0&&p->IfIndex==preferred)break;
    }
    free(list);return result;
#else
    char preferred[64]="",row[256];FILE *routes=fopen("/proc/net/route","r");
    if(routes){while(fgets(row,sizeof(row),routes)){
        char name[64];unsigned long destination,gateway;unsigned flags;
        if(sscanf(row,"%63s %lx %lx %x",name,&destination,&gateway,&flags)==4&&destination==0&&(flags&1)){
            snprintf(preferred,sizeof(preferred),"%s",name);break;
        }
    }fclose(routes);}
    struct ifaddrs *list=NULL;if(getifaddrs(&list))return -1;
    int result=-1;
    for(struct ifaddrs *p=list;p;p=p->ifa_next){
        if(!p->ifa_addr||p->ifa_addr->sa_family!=AF_INET)continue;
        if(!strcmp(p->ifa_name,"lo")||!strcmp(p->ifa_name,"lo0"))continue;
        struct sockaddr_in *a=(struct sockaddr_in *)p->ifa_addr;
        if(!flxtr_private_ipv4(ntohl(a->sin_addr.s_addr)))continue;
        if(result==0&&strcmp(p->ifa_name,preferred))continue;
        if(inet_ntop(AF_INET,&a->sin_addr,out,size)){result=0;if(!strcmp(p->ifa_name,preferred))break;}
    }
    freeifaddrs(list);return result;
#endif
}
#endif
