#!/bin/bash
#first build gcc

# needs root for systemctl/rmmod/insmod/bpf attach below; re-exec under sudo
# instead of failing partway through if invoked bare
if [[ $EUID -ne 0 ]]; then
    exec sudo "$0" "$@"
fi

exec > >(tee /root/target_vm.log) 2>&1

LIBGOMP_BIN_DIR=/home/debian/gcc-install/lib64

apt-get install -y systemtap-sdt-dev

cd /home/debian/gcc

git fetch

git reset --hard origin/phantom-libgomp

# gcc refuses in-tree builds — it must be built from a separate, already
# configured build directory, not from the source checkout itself
cd /home/debian/gcc-build
make -j$(nproc) && make install

cd /root/phantom-tracker/pvsched-shmem/guest

# cloud-init already builds/loads this module and starts omp_thread_reg.loader
# at boot; stop that instance first or rmmod fails with "module in use"
pkill -f omp_thread_reg.loader 2>/dev/null
sleep 1

# clear stale pinned progs/maps/links from the previous run — bpffs pins
# outlive the process that created them, so without this the kprobes/USDT
# probes from the last invocation stay attached alongside the new ones
rm -rf /sys/fs/bpf/*

make clean && make
rmmod guest_ivshmem 2>/dev/null; insmod guest_ivshmem.ko



cd /root/phantom-tracker/pvsched-ebpf/guest
make clean
make LIBGOMP_PATH="$LIBGOMP_BIN_DIR/libgomp.so.1"

# blocks forever to keep its USDT probe attached (libbpf can't pin that link
# type), so it must be backgrounded or this script never reaches barrier_sync.
# redirected to its own log instead of inheriting the tee pipe from the exec
# above — otherwise this process holds that pipe open forever and the ssh
# session running this script never sees EOF, even after the script exits
./omp_thread_reg.loader > /root/omp_thread_reg.log 2>&1 &

sleep 3
if [[ ! -e /sys/fs/bpf/omp_threads_map ]]; then
    echo "error: omp_thread_reg.loader did not pin /sys/fs/bpf/omp_threads_map" >&2
    echo "  it likely exited early (check the USDT attach output above)" >&2
    exit 1
fi

cd /root/phantom-tracker/test/barrier_sync
make clean
make LIBGOMP_PATH="$LIBGOMP_BIN_DIR/libgomp.so.1"

# kill any barrier_sync.loader left over from a previous manual run of this
# script — otherwise its probes stay attached and the new one below doubles
# up on top of it, double-counting sched_switch/futex/USDT events
pkill -f barrier_sync.loader 2>/dev/null
sleep 1

# like omp_thread_reg.loader, this blocks forever (sleep loop until
# SIGINT/SIGTERM) to keep its probes attached for the benchmark run that
# happens later via barrier_sync.sh — must be backgrounded, and redirected
# to its own log for the same reason as omp_thread_reg.loader above (otherwise
# it holds the tee pipe open and the ssh session running this script hangs)
../bin/barrier_sync.loader > /root/barrier_sync_loader.log 2>&1 &
sleep 3
