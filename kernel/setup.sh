#!/bin/sh
set -eu

GKI_ROOT=$(pwd)

display_usage() {
    echo "Usage: $0 [--cleanup | <commit-or-tag-or-branch>]"
    echo "  --cleanup:               Cleans up previous modifications made by the script."
    echo "  <commit-or-tag-or-branch>: Sets up or updates KernelSU to specified tag, commit, or branch."
    echo "  -h, --help:              Displays this usage information."
    echo "  (no args):               Sets up KernelSU to the KSUSUSFS branch by default."
}

initialize_variables() {
    if test -d "$GKI_ROOT/common/drivers"; then
         DRIVER_DIR="$GKI_ROOT/common/drivers"
    elif test -d "$GKI_ROOT/drivers"; then
         DRIVER_DIR="$GKI_ROOT/drivers"
    else
         echo '[ERROR] "drivers/" directory not found.'
         exit 127
    fi

    DRIVER_MAKEFILE=$DRIVER_DIR/Makefile
    DRIVER_KCONFIG=$DRIVER_DIR/Kconfig
}

# Reverts modifications made by this script
perform_cleanup() {
    echo "[+] Cleaning up..."
    [ -L "$DRIVER_DIR/kernelsu" ] && rm "$DRIVER_DIR/kernelsu" && echo "[-] Symlink removed."
    grep -q "kernelsu" "$DRIVER_MAKEFILE" && sed -i '/kernelsu/d' "$DRIVER_MAKEFILE" && echo "[-] Makefile reverted."
    grep -q "drivers/kernelsu/Kconfig" "$DRIVER_KCONFIG" && sed -i '/drivers\/kernelsu\/Kconfig/d' "$DRIVER_KCONFIG" && echo "[-] Kconfig reverted."
    if [ -d "$GKI_ROOT/KernelSU" ]; then
        rm -rf "$GKI_ROOT/KernelSU" && echo "[-] KernelSU directory deleted."
    fi
}

# Sets up or update KernelSU environment
setup_kernelsu() {
    TARGET_REF="${1:-KSUSUSFS}"

    echo "[+] Setting up KernelSU..."
    test -d "$GKI_ROOT/KernelSU" || git clone https://github.com/JeanneDarc-star/KernelSU && echo "[+] Repository cloned."
    cd "$GKI_ROOT/KernelSU"
    git stash && echo "[-] Stashed current changes."
    
    # Fetch all remote branches and tags explicitly
    git fetch --all --tags && echo "[+] Fetched latest refs."

    # Checkout requested target (branch, tag, or commit)
    if git checkout "$TARGET_REF" 2>/dev/null || git checkout -b "$TARGET_REF" "origin/$TARGET_REF" 2>/dev/null; then
        echo "[-] Checked out $TARGET_REF."
        git pull origin "$TARGET_REF" 2>/dev/null || true
    else
        echo "[-] Failed to checkout $TARGET_REF, using current HEAD."
    fi

    cd "$DRIVER_DIR"
    ln -sf "$(realpath --relative-to="$DRIVER_DIR" "$GKI_ROOT/KernelSU/kernel")" "kernelsu" && echo "[+] Symlink created."

    # Add entries in Makefile and Kconfig if not already existing
    grep -q "kernelsu" "$DRIVER_MAKEFILE" || printf "\nobj-\$(CONFIG_KSU) += kernelsu/\n" >> "$DRIVER_MAKEFILE" && echo "[+] Modified Makefile."
    grep -q "source \"drivers/kernelsu/Kconfig\"" "$DRIVER_KCONFIG" || sed -i "/endmenu/i\source \"drivers/kernelsu/Kconfig\"" "$DRIVER_KCONFIG" && echo "[+] Modified Kconfig."
    echo '[+] Done.'
}

# Process command-line arguments
if [ "$#" -eq 0 ]; then
    initialize_variables
    setup_kernelsu
elif [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    display_usage
elif [ "$1" = "--cleanup" ]; then
    initialize_variables
    perform_cleanup
else
    initialize_variables
    setup_kernelsu "$@"
fi
