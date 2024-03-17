import socket, os

# Define server address and port
SERVER_HOST = '127.0.0.1' # Localhost
SERVER_PORT = 4567

# Create a TCP/IP socket
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
    # Bind the socket to the server address and port
    server_socket.bind((SERVER_HOST, SERVER_PORT))

    # Listen for incoming connections
    server_socket.listen()

    print(f"Server listening on {SERVER_HOST}:{SERVER_PORT}")

    # Accept incoming connections
    client_socket, client_address = server_socket.accept()
    print(f"Connection established with {client_address}")

    # Receive messages indefinitely
    while True:
        try:
            # Receive data from the client
            data = client_socket.recv(1024)
            if not data:
                # If no data is received, the client has closed the connection
                print("Client disconnected")
                break

            print(f"Received message from client: {data}")

            # Send positive reply
            reply = "REPLY OK VSE JE OK\r\n"
            client_socket.send(reply.encode())  # Encode the reply string to bytes
        except Exception as e:
            print(f"Error occurred: {e}")
            break

    # Close the client socket
    client_socket.close()
