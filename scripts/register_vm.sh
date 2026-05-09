if [[ "$1" == --socket=* ]]; then
    VM_SOCKET="${1#--socket=}"
    echo "recieved $VM_SOCKET" > register_vm
else
    echo "Invalid argument: $1"
    exit 1
fi
