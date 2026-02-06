FROM ubuntu:24.04

# Install the LINUX RISC-V toolchain 
RUN apt-get update && apt-get install -y     gcc-riscv64-linux-gnu     libc6-dev-riscv64-cross     qemu-user     qemu-user-static     build-essential

WORKDIR /app
COPY audiomark.c .

# Compile with -static to bundle libraries
# Run 
CMD riscv64-linux-gnu-gcc -march=rv64gcv -mabi=lp64d -O3 -static -o audiomark audiomark.c &&     qemu-riscv64 -cpu rv64,v=true,vlen=128 ./audiomark
