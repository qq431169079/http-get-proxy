import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('127.0.0.1', 10001))
s.send('GET http://www.google.com/ HTTP/1.0\r\n\r\n')
print s.recv(1000000)
