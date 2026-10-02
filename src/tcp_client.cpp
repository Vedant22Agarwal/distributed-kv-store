
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

/*
 * ============================================================
 * PHASE 6B: IMPROVED TCP CLIENT
 * ============================================================
 *
 * Objective:
 *   1. Connect to the TCP server.
 *   2. Send messages reliably.
 *   3. Receive server responses.
 *   4. Handle connection errors.
 *   5. Close the socket properly.
 *
 */

/*
 * ============================================================
 * SEND ALL DATA
 * ============================================================
 *
 * TCP send() may send fewer bytes than requested.
 *
 * This function continues sending until all bytes
 * have been transmitted or an error occurs.
 *
 */

bool sendAll(
    int socketFd,
    const string &message)
{
    size_t totalSent = 0;

    while (totalSent < message.size())
    {
        ssize_t bytesSent = send(
            socketFd,
            message.c_str() + totalSent,
            message.size() - totalSent,
            0);

        if (bytesSent <= 0)
        {
            return false;
        }

        totalSent += bytesSent;
    }

    return true;
}

int main(int argc, char *argv[])
{

    if (argc != 3)
    {
        cerr << "Usage: ./tcp_client <ip> <port>\n";
        return 1;
    }

    string serverIP = argv[1];
    int serverPort = stoi(argv[2]);
    /*
     * ========================================================
     * CREATE TCP SOCKET
     * ========================================================
     */


    int clientSocket = socket(
        AF_INET,
        SOCK_STREAM,
        0);

    if (clientSocket == -1)
    {
        cerr << "Failed to create socket\n";
        return 1;
    }

    /*
     * ========================================================
     * CONFIGURE SERVER ADDRESS
     * ========================================================
     */

    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;

    serverAddress.sin_port = htons(serverPort);

    if (inet_pton(
            AF_INET,
            serverIP.c_str(),
            &serverAddress.sin_addr) <= 0)
    {
        cerr << "Invalid server IP address\n";

        close(clientSocket);

        return 1;
    }

    /*
     * ========================================================
     * CONNECT TO SERVER
     * ========================================================
     */

    if (connect(
            clientSocket,
            reinterpret_cast<sockaddr *>(&serverAddress),
            sizeof(serverAddress)) == -1)
    {
        cerr << "Connection to server failed\n";

        close(clientSocket);

        return 1;
    }

    cout << "\n========================================\n";
    cout << "       DISTRIBUTED KEY-VALUE STORE     \n";
    cout << "========================================\n";
    // cout << "Connected to server: 127.0.0.1:8080\n";
    cout << "Type 'exit' to quit\n";
    cout << endl;
    cout << "Available Commands:\n";
    cout << "  SET key value  - Store a value\n";
    cout << "  GET key        - Retrieve a value\n";
    cout << "  DEL key        - Delete a key\n";
    cout << "  PING           - Check server status\n";
    cout << "  exit           - Close connection\n";

    /*
     * ========================================================
     * SEND AND RECEIVE MESSAGES
     * ========================================================
     */

    string message;

    char buffer[1024]{};

    while (true)
    {
        cout << "\nEnter message (type exit to quit): ";

        if (!getline(cin, message))
        {
            break;
        }

        if (message == "exit")
        {
            break;
        }

        if (message.empty())
        {
            continue;
        }

        // Add newline for readable server-side logging.
        message += '\n';

        /*
         * ====================================================
         * SEND COMPLETE MESSAGE
         * ====================================================
         */

        if (!sendAll(clientSocket, message))
        {
            cerr << "Failed to send complete message\n";
            break;
        }

        // cout << "Message sent successfully\n";

        /*
         * ====================================================
         * RECEIVE RESPONSE
         * ====================================================
         */

        ssize_t bytesReceived = recv(
            clientSocket,
            buffer,
            sizeof(buffer) - 1,
            0);

        if (bytesReceived > 0)
        {
            buffer[bytesReceived] = '\0';

            cout
                << buffer;
        }

        else if (bytesReceived == 0)
        {
            cout << "Server closed the connection\n";
            break;
        }

        else
        {
            cerr << "Failed to receive response\n";
            break;
        }
    }

    /*
     * ========================================================
     * CLOSE CLIENT SOCKET
     * ========================================================
     */

    close(clientSocket);

    cout << "\nClient stopped.\n";

    return 0;
}