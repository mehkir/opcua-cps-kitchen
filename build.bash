#!/usr/bin/bash

cmake -B /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build -S /home/mehmet/vscode-workspaces/opcua-cps-kitchen
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target controller -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target robot -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target clock -- -j$(nproc)
