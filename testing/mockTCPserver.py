import socket, os

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind(('127.0.0.1', 4567))
sock.listen(5)

while True:
    connection,address = sock.accept()
    buf = connection.recv(1024)

    print(buf)

    connection.send(buf)
    connection.close()