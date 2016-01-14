// linkbox_server.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

// TODO load these from config file
#define PORT "3490"
#define BACKLOG 10
#define CHUNK_SIZE 4096

// TODO create a list of allowed clients and deny connections from other clients
// TODO create custom send and recv functions, error checks are constantly the same

void sigchld_handler(int s) {
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}

void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) 
		return &(((struct sockaddr_in*)sa)->sin_addr);

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// TODO split up this massive function
int main(void) {
	int sockfd, new_fd, numbytes, rv, yes = 1; 
	char s[INET6_ADDRSTRLEN];
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr;
	struct sigaction sa;
	socklen_t sin_size;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	// Use our local IP address for now, remove this line when working on external server
	hints.ai_flags = AI_PASSIVE;

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	// Cleanup the servinfo struct since we're done with it
	freeaddrinfo(servinfo);

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; 
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	for(;;) { 
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);

		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			uint32_t header_size_network[sizeof(uint32_t)], file_size_network[sizeof(uint32_t)];
			uint32_t header_size, file_size;

			// Receive the first sizeof(uint32_t) bytes which holds the header byte size
			if((recv(new_fd, header_size_network, sizeof(uint32_t), 0)) == -1) {
				perror("recv header size");
				exit(1);
			}

			// Receive the second sizeof(uint32_t) bytes which holds the file byte size
			if((recv(new_fd, file_size_network, sizeof(uint32_t), 0)) == -1) {
				perror("recv file size");
				exit(1);
			}

			// The size data is encoded using the htonl, so let's decode it again
			header_size = ntohl(*header_size_network);
			file_size = ntohl(*file_size_network);
			printf("Header & file size received from client: %i & %i bytes, reading header data \n", header_size, file_size);
			char header_data[header_size];

			// We now have the header and file sizes, let's receive the header data
			if(recv(new_fd, header_data, header_size, 0) == -1) {
				perror("recv header data");
				exit(1);
			}

			printf("Received header data: %s \n", header_data);

			numbytes = CHUNK_SIZE;

			/* Create and open the file and the file pointer for writing
			 * TODO check if the file already exists, if so generate a new name. Maybe generate a random name 
			 * in the first place (hashed clientID+filename+timestamp?)
			 * TODO put the file in the desired folder, make this a config file entry */
			FILE *fp;
			fp = fopen(header_data, "w+");

			/* If the amount of bytes we got are equal the the chunk size, we have stuff qeued, get this stream by
			 * calling recv() again. Repeat this until the received bytes aren't equal to the chunk size anymore, meaning
			 * the stream is finished */
			while(file_size > 0) {
				char new_chunk_data[CHUNK_SIZE];

				if((numbytes = recv(new_fd, new_chunk_data, CHUNK_SIZE, 0)) == -1) {
					perror("recv sub");
					exit(1);
				}

				fwrite(new_chunk_data, 1, numbytes, fp);

				printf("Subdata chunk received from client: (%i bytes) \n", numbytes);

				file_size -= numbytes;
			}

			// Close the file pointer since we're done with it
			fclose(fp);

			// TODO the url of the uploaded file back to the client
			close(new_fd);

			printf("File transmission successfully finished, closing file \n");

			break;
		}

		close(new_fd);  // parent doesn't need this
	}

	return 0;
}
