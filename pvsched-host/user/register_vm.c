// SPDX-License-Identifier: GPL-2.0
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
 
#include "linkedlist.h"
#include "register_vm.h"
 
#define QMP_BUFF_SIZE		4096
#define QMP_CONNECT_RETRY_S	1
 
static void print_vcpu(void *data)
{
	struct qmp_vpcu *v = (struct qmp_vpcu *)data;
 
	if (!v)
		return;
 
	printf("\ncpu=%d thread=%d\n", v->cpuIndex, v->threadId);
}
 
/*
 * extract_vcpu_info - parse a query-cpus-fast JSON response into a linked list.
 *
 * Returns a linked list of qmp_vpcu structs, or NULL on allocation failure.
 * The caller is responsible for freeing the list.
 */
static Node *extract_vcpu_info(const char *query_result)
{
	const char *entry_key = "{\"thread-id\": ";
	const char *cpu_key   = "\"cpu-index\": ";
	const char *str       = query_result;
	Node *head;
	
 
	head = create_node(NULL);
	if (!head) {
		fprintf(stderr, "extract_vcpu_info: failed to create list head\n");
		return NULL;
	}
 
	while ((str = strstr(str, entry_key)) != NULL) {
		struct qmp_vpcu *vcpu;
		const char *cpu_ptr;
		int thread_id = -1;
		int cpu_index = -1;
 
		str += strlen(entry_key);
		sscanf(str, "%d", &thread_id);
 
		cpu_ptr = strstr(str, cpu_key);
		if (cpu_ptr) {
			cpu_ptr += strlen(cpu_key);
			sscanf(cpu_ptr, "%d", &cpu_index);
		}
 
		vcpu = malloc(sizeof(*vcpu));
		if (!vcpu) {
			fprintf(stderr, "extract_vcpu_info: malloc failed\n");
			/*
			 * Return what we have so far rather than leaking
			 * the already-built list.
			 */
			return head;
		}
 
		vcpu->threadId = thread_id;
		vcpu->cpuIndex = cpu_index;
 
		push_back(&head, vcpu);
 
		printf("Thread ID: %d\n", thread_id);
		printf("CPU Index: %d\n\n", cpu_index);
	}
 
	print_nodes(head, print_vcpu);
 
	return head;
}
 
/*
 * qmp_connect - create and connect a UNIX socket to @path, retrying until
 * the peer is ready.
 *
 * Returns a valid fd on success, -1 on error.
 */
static int qmp_connect(const char *path)
{
	struct sockaddr_un addr;
	int fd;
 
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}
 
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
 
	if (strlen(path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "qmp_connect: socket path too long\n");
		close(fd);
		return -1;
	}
 
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
 
	printf("Connecting to socket");
	while (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf(".");
		fflush(stdout);
		sleep(QMP_CONNECT_RETRY_S);
	}
	printf("\nConnected to socket\n");
 
	return fd;
}
 
/*
 * qmp_send_recv - write @cmd to @fd and read the response into @buf.
 *
 * Returns 0 on success, -1 if the write or read failed.
 */
static int qmp_send_recv(int fd, const char *cmd, char *buf, size_t bufsz)
{
	ssize_t n;
 
	n = write(fd, cmd, strlen(cmd));
	if (n < 0) {
		perror("write");
		return -1;
	}
 
	memset(buf, 0, bufsz);
	n = read(fd, buf, bufsz - 1);
	if (n < 0) {
		perror("read");
		return -1;
	}
 
	return 0;
}
 
int main(int argc, char *argv[])
{
	const char *qmp_capabilities = "{ \"execute\": \"qmp_capabilities\" }";
	const char *query_cpus       = "{ \"execute\": \"query-cpus-fast\" }";
	char buff[QMP_BUFF_SIZE];
	const char *unix_socket;
	const char *vm_name;
	Node *vcpus;
	int sockfd;
	int ret = 1;
 
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <socket_path> <vm_name>\n", argv[0]);
		return 1;
	}
 
	unix_socket = argv[1];
	vm_name     = argv[2];
 
	sockfd = qmp_connect(unix_socket);
	if (sockfd < 0)
		return 1;
 
	/* 1. Read QMP greeting */
	memset(buff, 0, sizeof(buff));
	if (read(sockfd, buff, sizeof(buff) - 1) < 0) {
		perror("read greeting");
		goto cleanup_fd;
	}
	printf("QMP greeting: %s\n", buff);
 
	/* 2. Capability negotiation */
	if (qmp_send_recv(sockfd, qmp_capabilities, buff, sizeof(buff)) < 0)
		goto cleanup_fd;
	printf("Handshake response: %s\n", buff);
 
	/* 3. Query vCPU info */
	if (qmp_send_recv(sockfd, query_cpus, buff, sizeof(buff)) < 0)
		goto cleanup_fd;
	printf("VM status: %s\n", buff);
 
	vcpus = extract_vcpu_info(buff);
	if (!vcpus) {
		fprintf(stderr, "failed to extract vcpu info\n");
		goto cleanup_fd;
	}
 
	if (register_vm(vcpus, vm_name, unix_socket) < 0) {
		fprintf(stderr, "register_vm failed\n");
		goto cleanup_fd;
	}
 
	if (setup_vm_maps(vm_name) < 0) {
		fprintf(stderr, "setup_vm_maps failed\n");
		goto cleanup_fd;
	}
 
	ret = 0;
 
cleanup_fd:
	close(sockfd);
	return ret;
}