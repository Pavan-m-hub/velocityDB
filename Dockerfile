# 1. Use a lightweight Linux environment as our base OS
FROM ubuntu:22.04

# 2. Install the C++ compiler inside the container
RUN apt-get update && apt-get install -y g++

# 3. Create a working directory inside the container
WORKDIR /app

# 4. Copy your Mac's C++ code into the Linux container
COPY server.cpp .

# 5. Compile the code inside the Linux environment
# We add -pthread to ensure multithreading works in Linux
RUN g++ -pthread server.cpp -o server

# 6. Expose the ports we might use
EXPOSE 8081 8082

# 7. When the container boots, start the server as the LEADER by default
CMD ["./server", "8081"]