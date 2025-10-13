#!/bin/bash
set -euo pipefail

# PinnacleMM Cleanup Script

echo " PinnacleMM Cleanup Options:"
echo "1. Clean build files (keep source & config)"
echo "2. Clean logs only"
echo "3. Clean data/state only"
echo "4. Clean everything (nuclear option)"
echo "5. Show disk usage"
echo "6. Exit"

read -r -p "Choose option (1-6): " choice

case $choice in
    1)
        echo "Cleaning build files..."
        (cd build && make clean)
        echo "Build files cleaned"
        ;;
    2)
        echo "Cleaning logs..."
        rm -f build/*.log build/latency_results.txt
        echo "Logs cleaned"
        ;;
    3)
        echo "Cleaning data/state..."
        rm -rf build/data/*
        echo "Data/state cleaned"
        ;;
    4)
        echo "WARNING: This will delete the entire build directory!"
        read -r -p "Are you sure? (yes/NO): " confirm
        if [[ "$confirm" != "yes" ]]; then
            echo "Nuclear cleanup cancelled"
            exit 0
        fi
        echo "Nuclear cleanup - removing everything..."
        rm -rf build/*
        echo "Rebuilding..."
        mkdir -p build
        if ! (cd build && cmake .. && make -j4); then
            echo "ERROR: Rebuild failed. Check cmake and make output above."
            exit 1
        fi
        echo "Complete rebuild finished"
        ;;
    5)
        echo "Disk usage analysis:"
        echo "Build directory contents:"
        du -h build/* 2>/dev/null | sort -hr | head -10
        echo ""
        echo "Total build directory size:"
        du -sh build
        echo ""
        echo "Project root size:"
        du -sh .
        ;;
    6)
        echo "Cleanup cancelled"
        exit 0
        ;;
    *)
        echo "Invalid option"
        ;;
esac
