#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <unistd.h>
#include <cstring>
#include <unordered_map>
#include <sstream>
#include <thread>
#include <mutex>
#include <fstream> 
#include <chrono> 
#include <atomic> // PHASE 5: Atomic counters for blazing-fast metrics

using namespace std;
using namespace std::chrono; 

unordered_map<string, string> kv_store;
mutex db_mutex; 
string wal_filename; 
int forward_port = 0; 

enum Role { LEADER, FOLLOWER };
Role current_role = FOLLOWER; 
steady_clock::time_point last_heartbeat; 

// PHASE 5: The Metrics Trackers (Lock-free!)
atomic<int> total_requests(0);
atomic<int> total_sets(0);
atomic<int> total_gets(0);

void forward_command(const string& full_command) {
    if (forward_port == 0) return; 

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(forward_port);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0) {
        write(sock, full_command.c_str(), full_command.length());
    }
    close(sock);
}

void heartbeat_sender() {
    while (true) {
        if (current_role == LEADER && forward_port != 0) {
            forward_command("HEARTBEAT none none\n"); 
        }
        this_thread::sleep_for(milliseconds(50));
    }
}

void election_timer() {
    while (current_role == FOLLOWER) {
        this_thread::sleep_for(milliseconds(100)); 
        auto now = steady_clock::now();
        auto time_since_last_pulse = duration_cast<milliseconds>(now - last_heartbeat).count();

        if (time_since_last_pulse > 2000) {
            current_role = LEADER; 
            cout << "\n*** CRITICAL: LEADER TIMEOUT DETECTED! ***" << endl;
            cout << "*** AUTOMATIC FAILOVER TRIGGERED ***" << endl;
            cout << "*** PROMOTING MYSELF TO NEW LEADER! ***\n" << endl;
            break; 
        }
    }
}

void load_wal() {
    ifstream log_file(wal_filename);
    if (!log_file.is_open()) return;

    string line;
    int recovered_keys = 0;
    while (getline(log_file, line)) {
        stringstream ss(line);
        string command, key, value;
        ss >> command >> key >> value;
        if (command == "SET") {
            kv_store[key] = value;
            recovered_keys++;
        }
    }
    log_file.close();
    cout << "--- CRASH RECOVERY COMPLETE ---" << endl;
    cout << "Restored " << recovered_keys << " keys from " << wal_filename << endl;
}

void handle_client(int client_socket) {
    while (true) {
        char buffer[1024] = {0};
        int bytes_read = read(client_socket, buffer, 1024);
        if (bytes_read <= 0) break; 

        string input(buffer);
        
        // PHASE 5: The Raw HTTP Parser for Prometheus
        if (input.find("GET /metrics") == 0) {
            db_mutex.lock();
            int current_keys = kv_store.size();
            db_mutex.unlock();

            // Format the data exactly how Prometheus expects it
            string metrics = "# HELP kv_keys_total Total keys in the database\n";
            metrics += "# TYPE kv_keys_total gauge\n";
            metrics += "kv_keys_total " + to_string(current_keys) + "\n";
            
            metrics += "# HELP kv_requests_total Total requests processed\n";
            metrics += "# TYPE kv_requests_total counter\n";
            metrics += "kv_requests_total " + to_string(total_requests.load()) + "\n";

            metrics += "# HELP kv_sets_total Total SET commands\n";
            metrics += "# TYPE kv_sets_total counter\n";
            metrics += "kv_sets_total " + to_string(total_sets.load()) + "\n";

            // Wrap it in a valid HTTP 200 OK Response
            string http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n" + metrics;
            write(client_socket, http_response.c_str(), http_response.length());
            close(client_socket);
            return; // Kill connection after HTTP response
        }

        total_requests++; // Increment atomic counter

        stringstream ss(input);
        string command, key, value;
        ss >> command;
        string response;

        if (command == "HEARTBEAT") {
            last_heartbeat = steady_clock::now(); 
            response = "ACK\n";
        }
        else if (command == "SET") {
            total_sets++; // Increment atomic counter
            ss >> key >> value;
            
            db_mutex.lock();       
            kv_store[key] = value; 
            
            ofstream log_file(wal_filename, ios::app); 
            if (log_file.is_open()) {
                log_file << command << " " << key << " " << value << "\n";
                log_file.close();
            }
            db_mutex.unlock();     
            
            if (current_role == LEADER && forward_port != 0) {
                string repl_cmd = command + " " + key + " " + value + "\n";
                forward_command(repl_cmd);
            }
            response = "OK\n";
        } 
        else if (command == "GET") {
            total_gets++; // Increment atomic counter
            ss >> key;
            db_mutex.lock();       
            bool exists = kv_store.count(key);
            string retrieved_val;
            if (exists) retrieved_val = kv_store[key];
            db_mutex.unlock();     

            if (exists) response = retrieved_val + "\n";
            else response = "(nil)\n";
        } 
        else {
            response = "ERROR: Unknown command.\n";
        }

        write(client_socket, response.c_str(), response.length());
    }
    close(client_socket);
}

int main(int argc, char const *argv[]) {
    if (argc < 2) {
        cerr << "Usage: ./server <my_port> [forward_port]" << endl;
        return 1;
    }

    int port = stoi(argv[1]);
    wal_filename = "wal_" + to_string(port) + ".log";
    
    if (argc >= 3) {
        forward_port = stoi(argv[2]);
        current_role = LEADER; 
        cout << "Running as LEADER. Forwarding data to port " << forward_port << endl;
        
        thread hb_thread(heartbeat_sender);
        hb_thread.detach();
    } else {
        current_role = FOLLOWER;
        cout << "Running as FOLLOWER." << endl;
    }

    load_wal();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    memset(&address, 0, sizeof(address)); 
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port); 

    if (::bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) return 1;
    if (listen(server_fd, 10) < 0) return 1;

    cout << "Node Running! Listening on port " << port << "..." << endl;

    if (current_role == FOLLOWER) {
        last_heartbeat = steady_clock::now(); 
        thread election_thread(election_timer);
        election_thread.detach();
    }

    while (true) {
        int addrlen = sizeof(address);
        int client_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (client_socket < 0) continue;
        thread client_thread(handle_client, client_socket);
        client_thread.detach(); 
    }
    close(server_fd);
    return 0;
}