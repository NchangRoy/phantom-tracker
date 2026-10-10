ssh -p 2222 debian@localhost "sudo apt-get install -y trace-cmd"
ssh -p 2223 debian@localhost "sudo apt-get install -y trace-cmd"

# /tmp on this host is tmpfs (RAM-backed) and fills up fast with a
# multi-guest recording — use a directory backed by real disk instead.
mkdir -p /root/traces
cd /root/traces

# ============================================================
# DEFAULT RUN
# ============================================================
mkdir -p /tmp/traces
cd /tmp/traces

ssh -p 2222 debian@localhost " sudo pkill -f 'trace-cmd agent'"
ssh -p 2223 debian@localhost " sudo pkill -f 'trace-cmd agent'"

ssh -p 2222 debian@localhost " sudo trace-cmd agent -p 823 -D"
ssh -p 2223 debian@localhost " sudo trace-cmd agent -p 823 -D"

sleep 5


trace-cmd record -e sched -v \
    -A @3:823 --name vm1 \
    -e printk:console -e sys_enter_sched_setaffinity -e sys_enter_futex -e bpf_trace -e sched -v \
    -A @4:823 --name vm2 \
    -e sched -v 
   

# ============================================================
# PVSCHED RUN
# ============================================================

ssh -p 2222 debian@localhost " sudo pkill -f 'trace-cmd agent'"
ssh -p 2223 debian@localhost " sudo pkill -f 'trace-cmd agent'"

ssh -p 2222 debian@localhost " sudo trace-cmd agent -p 823 -D"
ssh -p 2223 debian@localhost " sudo trace-cmd agent -p 823 -D"

sleep 5

# vm1 (target, cid 3) is where the benchmark + bpf probes run, so it gets the
# printk/setaffinity/futex/bprint events; vm2 (noisy, cid 4) just runs
# stress-ng and stays plain sched. bpf_trace is bpf_printk()'s output —
# it lands in the global ring buffer regardless of -e, but listed explicitly
# here rather than relying on that. sched_stat_runtime is dropped everywhere
# — it dominated trace size without adding much signal.
trace-cmd record -e sched -v \
    -A @3:823 --name vm1 \
    -e printk:console -e sys_enter_sched_setaffinity -e sys_enter_futex -e bpf_trace -e sched -v \
    -A @4:823 --name vm2 \
    -e sched -v 
  
