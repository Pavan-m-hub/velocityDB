import socket
import time
import threading

# --- BENCHMARK CONFIGURATION ---
HOST = '127.0.0.1'
PORT = 8081             # Hitting the Leader to test the WAL and Replicator!
NUM_REQUESTS = 10000    # Total number of SET commands to send
CONCURRENCY = 100       # Number of simultaneous users (threads)

def worker(worker_id, requests_per_worker):
    try:
        # Each worker opens a persistent TCP connection to your C++ server
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORT))
        
        for i in range(requests_per_worker):
            # Generate a unique key for every single request
            cmd = f"SET loadtest_{worker_id}_{i} speed_demon\n"
            s.sendall(cmd.encode())
            
            # Wait for the C++ server to reply "OK\n" before sending the next one
            response = s.recv(1024) 
            
        s.close()
    except Exception as e:
        print(f"Worker {worker_id} Failed: {e}")

def run_benchmark():
    print(f"🔥 Starting Stress Test: {NUM_REQUESTS} SET operations...")
    print(f"🔥 Simulating {CONCURRENCY} concurrent users...\n")
    
    start_time = time.time()
    
    threads = []
    requests_per_thread = NUM_REQUESTS // CONCURRENCY
    
    # Unleash the army of threads
    for i in range(CONCURRENCY):
        t = threading.Thread(target=worker, args=(i, requests_per_thread))
        threads.append(t)
        t.start()
        
    # Wait for all threads to finish their attacks
    for t in threads:
        t.join()
        
    end_time = time.time()
    duration = end_time - start_time
    rps = NUM_REQUESTS / duration
    latency = (duration / NUM_REQUESTS) * 1000
    
    print("--- 🏁 BENCHMARK COMPLETE 🏁 ---")
    print(f"Total Time:       {duration:.2f} seconds")
    print(f"Throughput (RPS): {rps:.2f} Requests Per Second")
    print(f"Average Latency:  {latency:.2f} ms per request")

if __name__ == "__main__":
    run_benchmark()