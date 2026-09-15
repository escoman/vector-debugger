#!/bin/bash
cd /home/alexey/Projects/vector-debugger/debugger/build
# v06c-mcp target only exists when the AI agent is enabled at configure time.
cmake -DENABLE_AI_AGENT=ON ..
make clean
make v06c-debugger v06c-mcp -j$(nproc)
