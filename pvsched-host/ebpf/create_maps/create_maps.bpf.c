#include"vmlinux.h"
#include<bpf/bpf_helpers.h>
#include<bpf/bpf_tracing.h>
#include"register_vm_bpf.h"
#include"phantom_tracker.h"
char LICENSE[] SEC("license")= "GPL";


//map containing all vms

struct {
    __uint(type,BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10000000);
    __type(key, char[VM_NAME_LEN]);//vm name
    __type(value,struct vm_t);
} vms SEC(".maps");


//map containing all vcpus
struct {
    __uint(type,BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10000000);
    __type(key, __u32);//thread id
    __type(value,struct vcpu_t);
} vcpus SEC(".maps");



//map containing collection and processing maps






struct {
    __uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
    __uint(max_entries, 1024);
    __type(key, char[VM_NAME_LEN]);
    __type(value, __u32); // index into inner map array
} map_registry SEC(".maps");




/*

struct bpf_timer {
    __u64 :64;
    __u64 :64;
};

struct map_elem {
    int counter;
    struct bpf_timer timer;
};

struct{
    __uint(type,BPF_MAP_TYPE_ARRAY);
    __uint(max_entries,1024);
    __type(key,__u32);
    __type(value,struct map_elem);
} timer SEC(".maps");
*/



/*
struct trace_event_raw_sched_switch {
	struct trace_entry ent;
	char prev_comm[16];
	pid_t prev_pid;
	int prev_prio;
	long int prev_state;
	char next_comm[16];
	pid_t next_pid;
	int next_prio;
	char __data[0];
};
*/


/*
static int timer_callback(void *map, int *key, struct map_elem *val)
{
    bpf_printk("timer fired counter=%d\n", val->counter);

    val->counter++;

    restart timer every second 
    bpf_timer_start(&val->timer, 1000000000ULL, 0);

    return 0;
}

*/







SEC("tp/sched/sched_switch")
int  
handle_switch(struct  trace_event_raw_sched_switch * ctx)
{

    __u32 cpu;
    __u32 tid;
    __u64 ts;
    struct vcpu_t * vcpu;
    struct vm_t * vm;
    char buff[VM_NAME_LEN]={};
    int i = 0;
    __u32 idx;
    struct phantom_count count = {};
    void * collection_map;


   /*
    //initialize timer
    __u32 key = 0;

    struct map_elem init = {};
    struct map_elem *elem;
   */

	cpu=bpf_get_smp_processor_id(); 
    ts = bpf_ktime_get_ns();

     tid=ctx->next_pid;


      /* struct phantom_count count = {};
        elem = bpf_map_lookup_elem(&timer, &key);

        if (!elem) {
            bpf_map_update_elem(&timer, &key, &init, BPF_ANY);

            elem = bpf_map_lookup_elem(&timer, &key);

            if (!elem)
                return 0;

            bpf_timer_init(&elem->timer, &timer, 0);

            bpf_timer_set_callback(&elem->timer, timer_callback);

            bpf_timer_start(&elem->timer, 1000000000ULL, 0);
        }
      */

    
    vcpu=bpf_map_lookup_elem(&vcpus,&tid);
    
    if(vcpu!=NULL){
   
        //increment phantom count
        vm=bpf_map_lookup_elem(&vms,vcpu->vm_name);

       // bpf_printk("Entering here because of vcpu %d\n",vcpu->vcpu_index);
        if(vm!=NULL) 
        {
         vm->phantom_count++;
         //bpf_printk(" %llu ns on cpu %d phantom count %d \n", ts,cpu,vm->phantom_count);

         
         
         

        // copy vm_name safely
        #pragma clang loop unroll(full)
        for (i = 0; i < VM_NAME_LEN - 2; i++) {
            char c = vcpu->vm_name[i];
            buff[i] = c;
            if (c == '\0')
                break;
        }

        // append "_p"
        buff[i++] = '_';
        buff[i++] = 'p';
        buff[i] = '\0';

        collection_map=bpf_map_lookup_elem(&map_registry,buff);
        if(collection_map!=NULL){
            idx = vm->collection_index++;
           
            count.timestamp = bpf_ktime_get_ns();
            count.count = vm->phantom_count;
            

            bpf_map_update_elem(collection_map, &idx, &count, BPF_ANY);
         }
        }
    }



    return 0;

}

