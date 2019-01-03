Intorudction

This utility was tested with UBlox ZED-F9P GPS receiver and enables usage of its internal RTK engine. It is possible this works with other UBlox RTCM3-input enabled receivers but no guarantees.

To use this, you must have two receivers (one at base and one at "rover"), the one at base should be set to send RTCM3 messages. Don't forget to set its coordinates in TMODE configuration (static, surveying or moving)! The rover's receiver should be configured to enable RTCM3 input (by default they are). Both receivers are accessed through TCP/IP connections, that means you must install ser2net or similar utility to redirect serial port to the TCP/IP socket.

The utility has two modes - RTKLIB-compatible output (without deviation stuff, but with fix flag support) or pass-through, when you get just all stuff from receiver directly. It is switched by -p command line option.


Installation

Just clone the repository and compile/install with "make all"/"make install".


Usage
```
rtkzedf9p [-p] [-d] -b <BASE_RECEIVER_IP>:<BASE_RECEIVER_PORT> [ -r <ROVER_RECEIVER_IP>:<ROVER_RECEIVER_PORT> ] [ -l <LISTEN_PORT> ]

-p - enables pass-through mode
-d - enables debug output
-b - sets base receiver's IP and port (in form "192.168.1.182:3003")
-r - sets rover receiver's IP and port (same)
-l - sets listen port for output
```

