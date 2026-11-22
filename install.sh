#!/usr/bin/env bash

cd "$(dirname "$0")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "Verifying symlinks..."

# Fix symlinks if cloned on Windows or without symlink support
for dir in src/supla-dev/src/supla-client-lib; do
  if [ -d "$dir" ]; then
    (cd "$dir" && for f in *; do
      if [ -f "$f" ] && [ ! -L "$f" ]; then
        target=$(cat "$f" 2>/dev/null)
        if [[ "$target" == ../* ]]; then
          ln -sf "$target" "$f"
        fi
      fi
    done)
  fi
done

echo "Building. Be patient."

(cd src/supla-dev/Release && make all >/dev/null 2>&1 && cd ../../..) || exit 1

if [ ! -f supla-virtual-device ] && [ ! -L supla-virtual-device ]; then
  ln -sf src/supla-dev/Release/supla-virtual-device supla-virtual-device
fi

echo -e "${GREEN}OK!${NC}"
./supla-virtual-device -v

if [ ! -f supla-virtual-device.cfg ]; then
  cp supla-virtual-device.cfg.sample supla-virtual-device.cfg
  echo -e "${YELLOW}Sample configuration has been created for you (${NC}supla-virtual-device.cfg${YELLOW})${NC}"
  echo -e "${YELLOW}Adjust it to your needs before launching.${NC}"
fi
