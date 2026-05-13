#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<sys/un.h>
#include"linkedlist.h"
#include"register_vm.h"



void print_vcpu(void *data) {

    struct qmp_vpcu*v = (struct qmp_vpcu*)data;
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
        struct qmp_vpcu* vcpu=(struct qmp_vpcu*)malloc(sizeof(struct qmp_vpcu));
      
        vcpu->threadId = threadId;
        vcpu->cpuIndex = cpuIndex;

        push_back(&head, vcpu);

        printf("Thread ID: %d\n", threadId);

        printf("CPU Index: %d\n\n", cpuIndex);
    }
    return head;
    print_nodes(head, print_vcpu);
    
  
}

int main(int argc, char * argv[]) {
    char * unix_socket=argv[1];
    char * vm_name=argv[2];
    
    int socketfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketfd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, unix_socket);

    printf("Connecting to Socket..");
    while (connect(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
       
        printf(".");
        fflush(stdout);  
        sleep(1);
        
        
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
    Node  * vcpus= extract_vcpu_info(buff);
    
    printf(" hello world %p\n",vcpus);
    register_vm(vcpus,vm_name,unix_socket);

    return 0;


}