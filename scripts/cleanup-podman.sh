#!/bin/bash
#
# Gentle Podman cleanup script
# Removes only dangling/unused resources with confirmation
#

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Podman Gentle Cleanup Script ===${NC}\n"

# Function to ask for confirmation
confirm() {
    local prompt="$1"
    local response
    read -p "$prompt [y/N]: " response
    case "$response" in
        [yY][eE][sS]|[yY]) 
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

# 1. Check and remove stopped containers
echo -e "${YELLOW}>>> Checking for stopped containers...${NC}"
STOPPED_CONTAINERS=$(podman ps -a -f status=exited -q)
if [ -n "$STOPPED_CONTAINERS" ]; then
    echo "Found stopped containers:"
    podman ps -a -f status=exited --format "{{.ID}}\t{{.Names}}\t{{.Status}}"
    if confirm "Remove stopped containers?"; then
        podman container prune -f
        echo -e "${GREEN}✓ Stopped containers removed${NC}\n"
    else
        echo -e "Skipped.\n"
    fi
else
    echo -e "${GREEN}✓ No stopped containers found${NC}\n"
fi

# 2. Check and remove dangling images
echo -e "${YELLOW}>>> Checking for dangling images (untagged/intermediate layers)...${NC}"
DANGLING_IMAGES=$(podman images -f dangling=true -q)
if [ -n "$DANGLING_IMAGES" ]; then
    echo "Found dangling images:"
    podman images -f dangling=true --format "{{.ID}}\t{{.Repository}}\t{{.Size}}"
    if confirm "Remove dangling images?"; then
        podman image prune -f
        echo -e "${GREEN}✓ Dangling images removed${NC}\n"
    else
        echo -e "Skipped.\n"
    fi
else
    echo -e "${GREEN}✓ No dangling images found${NC}\n"
fi

# 3. Check and remove unused volumes
echo -e "${YELLOW}>>> Checking for unused volumes...${NC}"
UNUSED_VOLUMES=$(podman volume ls -q -f dangling=true)
if [ -n "$UNUSED_VOLUMES" ]; then
    echo "Found unused volumes:"
    podman volume ls -f dangling=true
    if confirm "Remove unused volumes?"; then
        podman volume prune -f
        echo -e "${GREEN}✓ Unused volumes removed${NC}\n"
    else
        echo -e "Skipped.\n"
    fi
else
    echo -e "${GREEN}✓ No unused volumes found${NC}\n"
fi

# 4. Check and remove build cache
echo -e "${YELLOW}>>> Checking build cache...${NC}"
if confirm "Clean build cache?"; then
    podman builder prune -f 2>/dev/null || echo "No build cache to clean (or builder command not available)"
    echo -e "${GREEN}✓ Build cache cleaned${NC}\n"
else
    echo -e "Skipped.\n"
fi

# 5. Check for orphaned overlay storage
echo -e "${YELLOW}>>> Checking for storage discrepancies...${NC}"
PODMAN_REPORTED=$(podman system df --format "{{.Size}}" | head -1 | grep -o '[0-9.]*')
ACTUAL_SIZE=$(du -sh ~/.local/share/containers/storage/ 2>/dev/null | awk '{print $1}')
echo "Podman reports: $(podman system df --format "{{.Size}}" | head -1)"
echo "Actual disk usage: $ACTUAL_SIZE"
echo ""

# If there's a huge discrepancy, offer aggressive cleanup
echo -e "${RED}⚠ WARNING: If actual usage >> reported, you have orphaned data${NC}"
echo "This happens when builds fail and leave unreferenced layers."
echo ""
if confirm "Run AGGRESSIVE cleanup? (removes all unused images, not just dangling)"; then
    echo -e "${YELLOW}Running aggressive cleanup...${NC}"
    podman system prune -a --volumes --force
    echo -e "${GREEN}✓ Aggressive cleanup complete${NC}\n"
else
    echo -e "Skipped aggressive cleanup.\n"
fi

# 6. Summary
echo -e "${GREEN}=== Cleanup Summary ===${NC}"
podman system df
echo ""
df -h /home | tail -1
echo ""
echo -e "${GREEN}Cleanup complete!${NC}"
echo ""
echo "Note: This script removes:"
echo "  - Stopped/exited containers"
echo "  - Dangling images (untagged intermediate layers)"
echo "  - Unused volumes"
echo "  - Build cache"
echo "  - Optionally: ALL unused images (aggressive mode)"
echo ""
echo "Tagged images IN USE and running containers are NOT touched."
echo ""
echo -e "${YELLOW}If disk is still full after aggressive cleanup:${NC}"
echo "  Nuclear option: podman system reset --force"
echo "  (WARNING: removes ALL podman data, you'll need to rebuild)"
