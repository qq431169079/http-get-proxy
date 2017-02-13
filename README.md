# http-get-proxy
A simple HTTP GET proxy server.

# Usage

```bash
git clone https://github.com/peterdelevoryas/http-get-proxy
cd http-get-proxy
make
./webproxy 10001
python
>>> import socket
>>> s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
>>> s.connect(('127.0.0.1', 10001))
>>> s.send('GET http://www.google.com/ HTTP/1.0\r\n\r\n')
>>> print s.recv(1024)
```

The above python test is available as "get_google.py"
in this repository.

You can also set Firefox to use it as a proxy server,
and it will work for some websites! A good one to
test on is www.example.org.

# Architecture

A simple thread-per-connection pattern is used.
Connection threads are implemented in a synchronous
fashion.
