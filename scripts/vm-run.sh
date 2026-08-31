#!/bin/bash

set -euo pipefail

SELF_DIR=$(dirname "$(realpath "$0")")
PROJECT_DIR=$(dirname "$SELF_DIR")
CONDY_VM_RUN="$PROJECT_DIR/third_party/condy/scripts/vm-run.sh"

MODE="privileged"
KVM_FLAG=""
COMMAND="/bin/sh"

usage() {
    echo "Usage: $0 [-hk] [-m <mode>] [-c <cmd>] <bzImage> [file]..."
    echo "Run condy's vm-run.sh with an ublk test environment."
    echo "  -h              show this help and exit"
    echo "  -k              enable KVM acceleration"
    echo "  -m <mode>       run mode: privileged (default) or unprivileged"
    echo "  -c <cmd>        command to run in the guest (default: interactive shell)"
    echo "  <bzImage>       kernel image to boot"
    echo "  [file]...       files to include in the initrd, placed in /root"
    exit 1
}

while getopts "hkm:c:" opt; do
    case $opt in
        h) usage ;;
        k) KVM_FLAG="-k" ;;
        m) MODE="$OPTARG" ;;
        c) COMMAND="$OPTARG" ;;
        *) usage ;;
    esac
done
shift $((OPTIND -1))

[ $# -lt 1 ] && usage
case "$MODE" in
    privileged|unprivileged) ;;
    *) echo "Error: unknown mode '$MODE'" >&2; exit 1 ;;
esac

KERNEL_IMAGE="$1"
FILES="${@:2}"

TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

EXTRA_FILES=""
if [ "$MODE" = unprivileged ]; then
    # Build a setup script that makes ublk device nodes accessible
    # to a non-root user
    cat > "$TEMP_DIR/ublk-setup.sh" << 'EOF'
#!/bin/sh
set -e
printf "ublk-control 0:0 666\nublkc[0-9]+ 1000:1000 660\nublkb[0-9]+ 1000:1000 660\n" > /etc/mdev.conf
echo /sbin/mdev > /proc/sys/kernel/hotplug
chmod 666 /dev/ublk-control
echo "ublk:x:1000:1000:ublk:/root:/bin/sh" >> /etc/passwd
echo "ublk:x:1000:" >> /etc/group
# Non-root io_uring buffer registration needs a large memlock limit.
ulimit -l unlimited

exec su ublk -c 'CMD'
EOF
    sed -i "s|CMD|$COMMAND|g" "$TEMP_DIR/ublk-setup.sh"
    chmod +x "$TEMP_DIR/ublk-setup.sh"
    GUEST_COMMAND="sh /root/ublk-setup.sh"
    EXTRA_FILES="$TEMP_DIR/ublk-setup.sh"
else
    GUEST_COMMAND="$COMMAND"
fi

[ -f "$CONDY_VM_RUN" ] || { echo "Error: $CONDY_VM_RUN not found (submodule?)" >&2; exit 1; }

bash "$CONDY_VM_RUN" $KVM_FLAG -c "$GUEST_COMMAND" "$KERNEL_IMAGE" $EXTRA_FILES $FILES
