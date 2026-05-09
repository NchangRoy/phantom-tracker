#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<sys/un.h>
#include"linkedlist.h"
struct vcpu_t {
    int cpuIndex;
    int threadId;
};

void print_vcpu(void *data) {

    struct vcpu_t *v = (struct vcpu_t *)data;
    if(v!=NULL){
          printf("\ncpu=%d thread=%d\n",
           v->cpuIndex,
           v->threadId);
    }
  
}


Node * extract_vcpu_info(char *query_result) {

    const char *entry_key  = "{\"thread-id\": ";
    const char *cpu_key    = "\"cpu-index\": ";

    char *str = query_result;

    struct Node * head=create_node(NULL);

    while ((str = strstr(str, entry_key)) != NULL) {

        int threadId = -1;
        int cpuIndex = -1;

        // move after '{"thread-id": '
        str += strlen(entry_key);

        // extract top-level thread-id
        sscanf(str, "%d", &threadId);

        // find cpu-index in same object
        char *cpu_ptr = strstr(str, cpu_key);

        if (cpu_ptr != NULL) {

            cpu_ptr += strlen(cpu_key);

            sscanf(cpu_ptr, "%d", &cpuIndex);
        }
        struct vcpu_t * vcpu=(struct vcpu_t *)malloc(sizeof(struct vcpu_t));
      
        vcpu->threadId = threadId;
        vcpu->cpuIndex = cpuIndex;

        push_back(&head, vcpu);

        printf("Thread ID: %d\n", threadId);

        printf("CPU Index: %d\n\n", cpuIndex);
    }

    print_nodes(head, print_vcpu);
    
    return head;
}

int main(int argc, char * argv[]) {
    char * unix_socket=argv[1];
    int socketfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketfd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, unix_socket);

    if (connect(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        exit(1);
    }

    printf("Connected to socket\n");

    char buff[4096];
    memset(buff, 0, sizeof(buff));

    // 1. READ QMP greeting
    read(socketfd, buff, sizeof(buff));
    printf("QMP greeting: %s\n", buff);

    // 2. SEND handshake
    const char *cap = "{ \"execute\": \"qmp_capabilities\" }";
    write(socketfd, cap, strlen(cap));

    memset(buff, 0, sizeof(buff));
    read(socketfd, buff, sizeof(buff));
    printf("Handshake response: %s\n", buff);

    // 3. QUERY STATUS
    const char *cmd = "{ \"execute\": \"query-cpus-fast\" }";
    write(socketfd, cmd, strlen(cmd));

    memset(buff, 0, sizeof(buff));
    read(socketfd, buff, sizeof(buff));
    printf("VM status: %s\n", buff);
    extract_vcpu_info(buff);

    return 0;


}