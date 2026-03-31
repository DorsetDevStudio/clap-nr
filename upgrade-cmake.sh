#!/usr/bin/env bash
# upgrade-cmake.sh - Install CMake 3.20+ on WSL/Ubuntu

set -euo pipefail

echo "======================================================================"
echo "Installing CMake 3.20+ from Kitware's APT repository"
echo "======================================================================"

# Remove old CMake
echo "Removing old CMake..."
apt-get remove -y cmake || true
apt-get autoremove -y

# Install prerequisites
echo "Installing prerequisites..."
apt-get update
apt-get install -y wget software-properties-common lsb-release gnupg

# Get Ubuntu version
UBUNTU_CODENAME=$(lsb_release -cs)
echo "Detected Ubuntu codename: $UBUNTU_CODENAME"

# Add Kitware's APT repository
echo "Adding Kitware APT repository..."
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null

echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $UBUNTU_CODENAME main" | tee /etc/apt/sources.list.d/kitware.list >/dev/null

# Update and install CMake
echo "Installing latest CMake..."
apt-get update
apt-get install -y cmake

# Verify installation
echo ""
echo "======================================================================"
CMAKE_VERSION=$(cmake --version | head -n1)
echo "✓ $CMAKE_VERSION installed successfully"
echo "======================================================================"

# Check if version is sufficient
if cmake --version | grep -q "version [3-9]\.[2-9][0-9]\|version [4-9]\."; then
    echo "✓ CMake version is sufficient (3.20+)"
else
    echo "⚠ Warning: CMake version may still be too old"
    cmake --version
fi
