#include "Network.hpp"

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Utils.hpp"

#define CHUNK_SIZE 4096

Network::Network(std::string server, std::string port) {
    this->server = server;
    this->port = port;
}

bool Network::init() {
    int rv;
    struct addrinfo hints, *servinfo, *p;
    char server_response[CHUNK_SIZE];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(this->server.c_str(), this->port.c_str(), &hints, &servinfo)) != 0) {
        Utils::error(gai_strerror(rv), true);
        return false;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((this->sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(this->sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(this->sockfd);
            continue;
        }

        break;
    }

    if (p == NULL)
        Utils::error("Failed to connect to server", true);

    freeaddrinfo(servinfo);

    return true;
}

bool Network::validate() {
    if (sockfd == 0) {
        this->error = "Initialize network before use";
        return false;
    }

    return true;
}

void Network::clean() {
    close(sockfd);
}

bool Network::sendData(const char* buf, size_t byte_size) {
    if (!validate())
        return false;

    ssize_t sent;

    while (byte_size > 0) {
        sent = send(this->sockfd, buf, byte_size, 0);

        if (sent <= 0)
            break;

        buf += sent;
        byte_size -= sent;
    }

    return true;
}

// TODO Use a fixed size header (256 bytes?)
// TODO Change header format to: file=<f_name>;size=<f_size>;auth=<user_token>;flags=<option_flags>
bool Network::sendHeader(const char* file_name, size_t file_size) {
    if (!validate())
        return false;

    size_t header_size = strlen(file_name) + 4;

    char header[header_size];
    strcpy(header, file_name);

    uint32_t header_size_network = htonl(header_size);
    uint32_t file_size_network = htonl(file_size);

    if (send(this->sockfd, &header_size_network, sizeof(header_size_network), 0) == -1)
        return false;

    if (send(this->sockfd, &file_size_network, sizeof(file_size_network), 0) == -1)
        return false;

    if (send(this->sockfd, header, header_size, 0) == -1)
        return false;

    return true;
}

bool Network::sendFile(const char* file_name) {
    if (!validate())
        return false;

    if (!Utils::hasPermissions(file_name, Utils::P_READ)) {
        Utils::error("File not found or insufficient permissions");
        return false;
    }

    size_t file_size = Utils::getFileSize(file_name);
    if (!sendHeader(file_name, file_size)) {
        this->error = "Failed to send header";
        return false;
    }

    std::ifstream fin(file_name, std::ifstream::binary);
    char buffer[CHUNK_SIZE];

    size_t total_sent = 0;
    float last_progress = 0.0f;
    int bar_width = 70;
    unsigned int read_size = file_size > sizeof(buffer) ? sizeof(buffer) : file_size;

    while (read_size != 0 && (fin.read(buffer, read_size))) {
        std::streamsize s = fin.gcount();

        if (!sendData(buffer, s)) {
            this->error = "Failed to send all data";
            return false;
        }

        total_sent += s;
        read_size = file_size - total_sent > sizeof(buffer) ? sizeof(buffer) : file_size - total_sent;

        // Progress bar ------
        float progress = (float) total_sent / (float) file_size;
        if (int(progress * 100.0) == last_progress)
            continue;

        std::cout << "[";
        int pos = bar_width * progress;
        for (int i = 0; i < bar_width; ++i)
            std::cout << (i < pos ? "=" : (i == pos ? ">" : " "));

        std::cout << "] " << int(progress * 100.0) << "%\r" << std::flush;

        last_progress = int(progress * 100.0);
    }

    std::cout << "[";
    for (int i = 0; i < bar_width; ++i)
        std::cout << "=";
    std::cout << "] " << "100%" << std::endl;

    return true;
}

bool Network::receive(char* buf, size_t byte_size) {
    if (!validate())
        return false;

    if (recv(sockfd, buf, byte_size, 0) == -1)
        return false;

    return true;
}

