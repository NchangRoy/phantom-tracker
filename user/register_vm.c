#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "linkedlist.h"

struct vcpu_t {
	int cpuIndex;
	int threadId;
};

/*
 * Inputs: data - pointer to vcpu_t struct containing CPU info
 * Outputs: None
 * Description: Prints the CPU index and thread ID for a given vCPU node
 */
static void print_vcpu(void *data)
{
	struct vcpu_t *v = (struct vcpu_t *)data;

	if (v == NULL)
		return;

	printf("\ncpu=%d thread=%d\n", v->cpuIndex, v->threadId);
}

/*
 * Inputs: query_result - JSON response string from QMP query-cpus-fast
 * Outputs: Returns a linked list of vcpu_t structs
 * Description: Parses a QMP query-cpus-fast JSON response to extract thread-id and cpu-index into a linked list
 */
static Node *extract_vcpu_info(char *query_result)
{
	const char *entry_key = "{\"thread-id\": ";
	const char *cpu_key = "\"cpu-index\": ";
	char *str = query_result;
	struct Node *head;

	head = create_node(NULL);
	if (head == NULL) {
		fprintf(stderr,
			"out of memory\n"); // TODO: improve error handling
		return;
	}

	while ((str = strstr(str, entry_key)) != NULL) {
		struct vcpu_t *vcpu;
		int threadId = -1;
		int cpuIndex = -1;
		char *cpu_ptr;

		// move after '{"thread-id": '
		str += strlen(entry_key);

		// extract top-level thread-id
		sscanf(str, "%d", &threadId);

		// find cpu-index in same object
		cpu_ptr = strstr(str, cpu_key);
		if (cpu_ptr != NULL) {
			cpu_ptr += strlen(cpu_key);

			sscanf(cpu_ptr, "%d", &cpuIndex);
		}

		vcpu = (struct vcpu_t *)malloc(sizeof(*vcpu));
		// TODO: out of memory handling / NULL pointer check.

		vcpu->threadId = threadId;
		vcpu->cpuIndex = cpuIndex;

		push_back(&head, vcpu);

		printf("Thread ID: %d\n", threadId);
		printf("CPU Index: %d\n\n", cpuIndex);
	}

	return head;
}

#define BUFSIZE 4096

/*
 * Inputs: argc - number of command line arguments
 *         argv - array of command line arguments
 * Outputs: Returns 0 on success, 1 on failure
 * Description: Main function to connect to a VM's QMP socket via UNIX domain socket and query vCPU information
 */
int main(int argc, char *argv[])
{
	const char *cap = "{ \"execute\": \"qmp_capabilities\" }";
	const char *cmd = "{ \"execute\": \"query-cpus-fast\" }";

	char buff[BUFSIZE];
	struct sockaddr_un addr = {};
	struct Node *head;
	char *unix_socket = argv[1];

	int socketfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socketfd < 0) {
		perror("socket");
		exit(1);
	}

	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, unix_socket);

	// TODO: retry if connect fails?
	if (connect(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("connect");
		exit(1);
	}

	printf("Connected to socket\n");

	// Read QMP greeting
	memset(buff, 0, sizeof(buff));
	read(socketfd, buff, sizeof(buff)); // TODO: error handling
	printf("QMP greeting: %s\n", buff);

	// Send handshake
	write(socketfd, cap, strlen(cap)); // TODO: error handling

	// Reading handshake response
	memset(buff, 0, sizeof(buff));
	read(socketfd, buff, sizeof(buff)); // TODO: error handling
	printf("Handshake response: %s\n", buff);

	// Query status
	write(socketfd, cmd, strlen(cmd)); // TODO: error handling

	// Read status
	memset(buff, 0, sizeof(buff));
	read(socketfd, buff, sizeof(buff)); // TODO: error handling
	printf("VM status: %s\n", buff);

	// Print CPU information
	head = extract_vcpu_info(buff);
	print_nodes(head, print_vcpu);

	return 0;
}
