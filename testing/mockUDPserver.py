import socket

localIP     = "127.0.0.1"
localPort   = 4567
bufferSize  = 1024

# Create a datagram socket
UDPServerSocket = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM)

# Bind to address and ip
UDPServerSocket.bind((localIP, localPort))

print("UDP server up and listening")
alreadySent = False

# Listen for incoming datagrams
while(True):
    bytesAddressPair = UDPServerSocket.recvfrom(bufferSize)

    message = bytesAddressPair[0]
    address = bytesAddressPair[1]

    clientMsg = "Message from Client:{}".format(message)
    clientIP  = "Client IP Address:{}".format(address)

    print(clientMsg)

    # Send confirm
    # Send reply


    # Send only once
    alreadySent = True
    # Sending a reply to client
    # Firstly send confimation msg

    if message == b'\x00\x00\x00':
        continue

    confirm = b'\x00\x00\x00'
    UDPServerSocket.sendto(confirm, address)

    reply = b'\x01\x00\x00\x00\x00\x00yes\x00'
    UDPServerSocket.sendto(reply, address)

    # Now send reply msg to authenticate user
    #reply_msg = "101100" + "Hello from server"
    #bytesToSend   = str.encode(reply_msg)
    #UDPServerSocket.sendto(bytesToSend, address)
