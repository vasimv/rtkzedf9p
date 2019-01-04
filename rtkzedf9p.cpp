// This program sends RTCM3 stream from base to the rover's receiver and translates
// NMEA output to the RTKLIB's compatible output
// by Vasim V.

#include <stdio.h>
#include "TinyGPS++.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
// #include <termios.h>
#include <poll.h>
#include <cstdlib>

char *roverReceiver = "127.0.0.1:3003";
char *baseReceiver = "192.168.1.182:3003";
int listenPort = 8988;

int sListen = -1;
int sIncoming = -1;

int debug = 0;

// Pass through mode (will give direct output, not RTKLIB's format)
int passthrough = 0;

struct timeval lastBaseConnect;
struct timeval lastRoverConnect;

int diffMillis(struct timeval tv) {
  struct timeval cTv, rTv;

  gettimeofday(&cTv, NULL);
  timersub(&cTv, &tv, &rTv);
  return (rTv.tv_usec / 1000) + (rTv.tv_sec * 1000);
}

void closeSock(int sock) {
  if (sock <= 0)
    return;
  close(sock);
  if (debug)
    fprintf(stderr, "Closing socket fd %d\n", sock);
}

struct KeepConfig {
    /** The time (in seconds) the connection needs to remain 
     * idle before TCP starts sending keepalive probes (TCP_KEEPIDLE socket option)
     */
    int keepidle;
    /** The maximum number of keepalive probes TCP should 
     * send before dropping the connection. (TCP_KEEPCNT socket option)
     */
    int keepcnt;

    /** The time (in seconds) between individual keepalive probes.
     *  (TCP_KEEPINTVL socket option)
     */
    int keepintvl;
};

/**
* enable TCP keepalive on the socket
* @param fd file descriptor
* @return 0 on success -1 on failure
*/
int set_tcp_keepalive(int sockfd) {
    int optval = 1;

    return setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}

/** Set the keepalive options on the socket
* This also enables TCP keepalive on the socket
*
* @param fd file descriptor
* @param fd file descriptor
* @return 0 on success -1 on failure
*/
int set_tcp_keepalive_cfg(int sockfd, int keepidle, int keepcnt, int keepintvl) {
    int rc;
    struct KeepConfig cfg;

    cfg.keepidle = keepidle;
    cfg.keepcnt = keepcnt;
    cfg.keepintvl = keepintvl;

    //first turn on keepalive
    rc = set_tcp_keepalive(sockfd);
    if (rc != 0) {
        return rc;
    }

    //set the keepalive options
    rc = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &cfg.keepcnt, sizeof(cfg.keepcnt));
    if (rc != 0) {
        return rc;
    }

    rc = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &cfg.keepidle, sizeof(cfg.keepidle));
    if (rc != 0) {
        return rc;
    }

    rc = setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &cfg.keepintvl, sizeof(cfg.keepintvl));
    if (rc != 0) {
        return rc;
    }

    return 0;
}

// Create listen socket
void openListen() {
  int opt = 1;
  struct sockaddr_in address;
  int sock = -1;

  if (debug)
    fprintf(stderr, "Opening listen socket\n");
  while (sock < 0) {
    // create socket
    if ((sock = socket(AF_INET , SOCK_STREAM , 0)) <= 0) {
      if (debug)
        perror("socket failed");
      sock = -1;
      continue;
    }

    // set master socket to allow multiple connections
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
      closeSock(sock);
      sock = -1;
      if (debug)
        perror("setsockopt");
      continue;
    }

    // Set type of socket created
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(listenPort);

    // bind the socket
    if (bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
      if (debug)
        perror("bind failed");
      closeSock(sock);
      sock = -1;
    }
    listen(sock, 5);
  }
  sListen = sock;
  if (debug) 
    fprintf(stderr, "Listen socked fd %d\n", sock);
}

// Accept incoming connection
void acceptConn() {
  struct sockaddr_in address;
  int addrlen = sizeof(address);

  if (sIncoming >= 0)
    closeSock(sIncoming);
  bzero(&address, sizeof(address));
  sIncoming = accept(sListen, (struct sockaddr *) &address, (socklen_t *) &addrlen);
  if (sIncoming > 0) {
    if (debug)
      fprintf(stderr, "New connection , socket fd is %d , ip is : %s , port : %d \n" , sIncoming , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));

    set_tcp_keepalive_cfg(sIncoming, 10, 5, 5);
  } else
    sIncoming = -1;
}

unsigned long millis() {
  struct timeval cTv;

  gettimeofday(&cTv, NULL);
  return cTv.tv_usec / 1000 + cTv.tv_sec * 1000;
}

double radians(double degrees) {
  return (degrees * M_PI) / 180.0;
}

double degrees(double radians) {
  return (radians * 180 ) / M_PI;
}

TinyGPSPlus gps;

int sZED = -1;
int sRTCM = -1;
int flagBaseConnecting = 0;

// New connection (returns socket)
int createConn(char *addrPort, int nonblock) {
  int sockfd;
  struct hostent *he;
  struct sockaddr_in their_addr; /* connector's address information */
  char host[256];
  int port;
  char *p;

  strncpy(host, addrPort, sizeof(host) - 1);
  host[sizeof(host) - 1] = '\0';
  // let's split address to host and port parts
  if (p = strchr(host, ':')) {
    *p = '\0';
    port = std::atoi(p + 1);
  } else
    port = 3003; 

  if (debug)
    fprintf(stderr, "Connecting to %s, port %d\n", host, port);

  if ((he=gethostbyname(host)) == NULL) {  /* get the host info */
    if (debug)
      herror("gethostbyname");
    return -1;
  }

  if ((sockfd = socket(AF_INET, nonblock ? (SOCK_STREAM | SOCK_NONBLOCK) : SOCK_STREAM, 0)) == -1) {
    if (debug)
      perror("socket");
    return -1;
  }

  bzero(&their_addr, sizeof(their_addr));
  their_addr.sin_family = AF_INET;
  their_addr.sin_port = htons(port);
  their_addr.sin_addr = *((struct in_addr *) he->h_addr);
  bzero(&(their_addr.sin_zero), 8);

  if ((connect(sockfd, (struct sockaddr *) &their_addr, sizeof(struct sockaddr)) == -1) && (errno != EINPROGRESS)) {
    close(sockfd);
    if (debug)
      perror("connect");
    return -1;
  }
  set_tcp_keepalive_cfg(sockfd, 10, 5, 5);
  return sockfd;
}

char c;
char buf[2048];

// Print out data in RTKLIB format
int printRtk(int sock) {
  char bufOut[2048];
  int fixFlag;

  // Translate from Ublox fix flag to RTKLIB
  switch (gps.fixFlag) {
    // RTK Fix
    case 4:
      fixFlag = 1;
      break;
    // RTK float
    case 5:
      fixFlag = 2;
      break;
    // DGPS
    case 3:
    case 2:
      fixFlag = 4;
      break;
    default:
      fixFlag = 5;
      break;
  }
  // sdn/sde/adu/adne/sdeu/sdun is fake
  snprintf(bufOut, sizeof(bufOut) - 1, "%04d/%02d/%02d %02d:%02d:%02d.%03d % 4.9f % 4.9f % 6.4f %d %d 0.0144 0.0154 0.0776 -0.0058 0.0082 -0.0199 0.00 % 3.3f\n",
    gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second(), gps.time.centisecond() * 10,
    gps.location.lat(), gps.location.lng(), gps.altitude.meters(), fixFlag, gps.satellites.value(), (fixFlag == 1) ? 999.900 : 1.500);

  if (debug)
    fprintf(stderr, "out: %s", bufOut);
  if (sock < 0)
    return 1;
  return send(sock, bufOut, strlen(bufOut), MSG_NOSIGNAL); 
}

void commPoll() {
  struct pollfd fds[4];
  int i, rc, res;

  memset(fds, 0, sizeof(fds));

  if (sZED >= 0) {
    fds[0].fd = sZED;
    fds[0].events = POLLIN;
  } else
    fds[0].fd = -1;

  if (sRTCM >= 0) {
    fds[1].fd = sRTCM;
    if (flagBaseConnecting)
      fds[1].events = POLLOUT;
    else
      fds[1].events = POLLIN;
  } else
    fds[1].fd = sRTCM;

  fds[2].fd = sListen;
  fds[2].events = POLLIN;

  if (sIncoming >= 0) {
    fds[3].fd = sIncoming;
    fds[3].events = POLLNVAL | POLLERR;
  } else
    fds[3].fd = -1;

  rc = poll(fds, 4, 10);
  if (rc < 0)
    return;

  if (fds[0].revents & POLLIN) {
    // Read from ZED-F9P
    res = read(sZED, &c, 1);
    if (res > 0) {
      // printf("%c", c);
      if (!passthrough) {
        gps.encode(c);
        if (gps.location.isUpdated()) {
          if (printRtk(sIncoming) <= 0) {
            if (debug)
              fprintf(stderr, "closing incoming socket\n");
            closeSock(sIncoming);
            sIncoming = -1;
          }
        }
      } else
        send(sIncoming, &c, 1, MSG_NOSIGNAL);
    } else {
      closeSock(sZED);
      sZED = -1;
    }
  }

  if ((fds[3].revents & POLLNVAL) || (fds[3].revents & POLLERR) || (fds[3].revents & POLLHUP)) {
    if (debug)
      fprintf(stderr, "closing incoming socket\n");
    closeSock(sIncoming);
    sIncoming = -1;
  }

  if (fds[1].revents & POLLIN) {
    // read RTCM
    res = recv(sRTCM, buf, sizeof(buf), MSG_NOSIGNAL);
    if (res > 0) {
      send(sZED, buf, res, MSG_NOSIGNAL);
    } else {
      closeSock(sRTCM);
      sRTCM = -1;
    }
  }

  if (fds[1].revents & POLLOUT) {
    int sockErr = 0;
    socklen_t sockErrLen = sizeof(sockErr);

    if (debug)
      fprintf(stderr, "Completing connection to base\n");
    
    res = getsockopt(sRTCM, SOL_SOCKET, SO_ERROR, &sockErr, &sockErrLen);
    if ((res >= 0) && (sockErr == 0)) {
      if (debug)
        fprintf(stderr, "Successful connection to base\n");
      flagBaseConnecting = 0;
    } else {
      if (debug)
        fprintf(stderr, "Wasn't able to connect to base\n");
      closeSock(sRTCM);
      sRTCM = -1;
      flagBaseConnecting = 0;
    }
  }

  if ((fds[1].revents & POLLNVAL) || (fds[1].revents & POLLERR) || (fds[1].revents & POLLHUP)) {
    closeSock(sRTCM);
    sRTCM = -1;
  }

  if ((sRTCM < 0) && (diffMillis(lastBaseConnect) > 2000)) {
    gettimeofday(&lastBaseConnect, NULL);
    sRTCM = createConn(baseReceiver, 1);
    flagBaseConnecting = 1;
  }

  if ((sZED < 0) && (diffMillis(lastRoverConnect) > 1000)) {
    gettimeofday(&lastRoverConnect, NULL);
    sZED = createConn(roverReceiver, 0);
  }
  
  if (fds[2].revents & POLLIN) {
    if (debug)
      fprintf(stderr, "Incoming connection\n");
    closeSock(sIncoming);
    sIncoming = -1;
    acceptConn();
  }
}

int main(int argc, char **argv) {
  int c;

  while ((c = getopt(argc, argv, "r:b:l:hdp")) != -1) {
    switch (c) {
      case 'r':
        if (optarg)
          roverReceiver = optarg;
        break;
      case 'b':
        if (optarg)
          baseReceiver = optarg;
        break;
      case 'l':
        if (optarg)
          listenPort = std::atoi(optarg);
        break;
      case 'd':
        debug = 1;
        break;
      case 'p':
        passthrough = 1;
        break;
      case 'h':
      default:
        fprintf(stderr, "Usage: %s [-p] [-d] -b <BASE_RECEIVER_IP>:<BASE_RECEIVER_PORT> [ -r <ROVER_RECEIVER_IP>:<ROVER_RECEIVER_PORT> ] [ -l <LISTEN_PORT> ]\n", argv[0]);
        fprintf(stderr, "-p - Pass-through mode, will output stuff from receiver directly, not in RTKLIB's format\n");
        fprintf(stderr, "-d - Debug output to stderr\n");
        fprintf(stderr, "Defaults: %s -b %s -r %s -l %d\n\n", argv[0], baseReceiver, roverReceiver, listenPort);
        fprintf(stderr, "The base receiver must be configured to output RTCM3 messages and have set its coordinates (in TMODE configuration section)!\n");
        exit(0);
        break;
    }
  }

  gettimeofday(&lastBaseConnect, NULL);
  gettimeofday(&lastRoverConnect, NULL);

  sZED = createConn(roverReceiver, 0);
  openListen();

  if (debug)
    fprintf(stderr, "Entering loop\n");
  while (1) {
    commPoll();
  }
}
