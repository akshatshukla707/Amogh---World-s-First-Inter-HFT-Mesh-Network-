// ================================================================
//  AMOGH — HACKATHON LIVE PROTOTYPE
//  Unified Architecture Demo: SCP-L2 + Registry + Routing
//  Compile: g++ -std=c++17 -O3 amogh_demo.cpp -o amogh_demo
//  Run:     ./amogh_demo
// ================================================================

#include <iostream>
#include <iomanip>
#include <string>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>

// Terminal Colors for presentation
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define CYAN    "\033[36m"
#define MAGENTA "\033[35m"
#define DIM     "\033[2m"

void pause_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ================================================================
// PART 1: THE SCP-L2 PROTOCOL (Zero-Copy Frame Structure)
// ================================================================
static constexpr uint64_t SHARED_SECRET = 0xC0FFEE1337BABE42ULL;

inline uint64_t generate_prime(uint64_t shared_secret, uint64_t seq) {
    uint64_t x = shared_secret ^ (seq * 0xDEADBEEFCAFEBABEULL);
    x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    return x;
}

#pragma pack(push, 1)
struct SCPFrame {
    uint8_t   dest_mac[6];     // Ethernet Header
    uint8_t   src_mac[6];
    uint16_t  ethertype;       // 0x88B5
    uint64_t  prime_id;        // 8 bytes: synchronized tag
    uint8_t   msg_type;        // 1 byte: 1=REAL, 0=DECOY
    uint32_t  firm_id;         // 4 bytes: sender ID
    uint64_t  seq_num;         // 8 bytes: sequence
    char      payload[49];     // 49 bytes: execution logic
    uint32_t  crc32;           // 4 bytes: integrity
};
#pragma pack(pop)

// ================================================================
// PART 2: THE CAPABILITY REGISTRY (Sorting & State)
// ================================================================
enum class FirmStatus { FREE, BUSY, CAPITAL_EXHAUSTED, OFFLINE };

struct CapabilityEntry {
    uint32_t   firm_id;
    std::string name;
    double     capital_usd;
    uint32_t   latency_us;
    uint32_t   slots;
    FirmStatus status;
    double     fitness_score;

    void compute_fitness() {
        if (status != FirmStatus::FREE) {
            fitness_score = 0.0;
            return;
        }
        // Simplified weights: 50% Capital, 30% Speed, 20% Capacity
        double cap_score = std::min(capital_usd / 500000.0, 1.0);
        double lat_score = std::max(0.0, 1.0 - (latency_us / 1000.0)); 
        double slot_score = std::min(slots / 50.0, 1.0);
        fitness_score = (0.50 * cap_score) + (0.30 * lat_score) + (0.20 * slot_score);
    }
};

class AmoghHub {
public:
    CapabilityEntry registry[6];

    void sort_registry() {
        std::sort(registry, registry + 6, [](const CapabilityEntry& a, const CapabilityEntry& b) {
            return a.fitness_score > b.fitness_score;
        });
    }

    void print_dashboard(const std::string& title) {
        std::cout << "\n" << CYAN << BOLD << "=====================================================================\n";
        std::cout << "  AMOGH HUB :: CAPABILITY REGISTRY  |  " << title << "\n";
        std::cout << "=====================================================================\n" << RESET;
        std::cout << BOLD << std::left << std::setw(6) << "RANK" << std::setw(10) << "FIRM" 
                  << std::setw(14) << "CAPITAL" << std::setw(10) << "LATENCY" 
                  << std::setw(20) << "STATUS" << "FITNESS\n" << RESET;
        std::cout << "---------------------------------------------------------------------\n";

        for (int i = 0; i < 6; i++) {
            auto& e = registry[i];
            if (i == 0 && e.fitness_score > 0) std::cout << GREEN << BOLD << " ★ 1  ";
            else std::cout << "   " << i + 1 << "  ";

            std::cout << RESET << std::left << std::setw(10) << e.name 
                      << "$" << std::setw(13) << (int)e.capital_usd 
                      << e.latency_us << std::setw(7) << "us";

            if (e.status == FirmStatus::FREE) std::cout << GREEN << std::setw(20) << "FREE";
            else if (e.status == FirmStatus::BUSY) std::cout << YELLOW << std::setw(20) << "BUSY";
            else if (e.status == FirmStatus::CAPITAL_EXHAUSTED) std::cout << RED << std::setw(20) << "CAPITAL EXHAUSTED";
            else std::cout << DIM << std::setw(20) << "OFFLINE";

            if (e.fitness_score > 0) std::cout << GREEN << std::fixed << std::setprecision(2) << e.fitness_score << RESET << "\n";
            else std::cout << RED << "0.00" << RESET << "\n";
        }
        std::cout << "---------------------------------------------------------------------\n\n";
    }
};

// ================================================================
// PART 3: THE LIVE SIMULATION SCENARIO
// ================================================================
int main() {
    std::cout << "\033[H\033[2J"; // Clear screen
    
    AmoghHub hub;
    
    // Initialize 6 Firms with Dummy Data
    hub.registry[0] = {1, "Firm 1", 120000, 150, 50, FirmStatus::FREE, 0};
    hub.registry[1] = {2, "Firm 2", 500000, 180, 35, FirmStatus::FREE, 0};
    hub.registry[2] = {3, "Firm 3", 0,      140, 20, FirmStatus::BUSY, 0};
    hub.registry[3] = {4, "Firm 4", 300000, 200, 40, FirmStatus::FREE, 0};
    hub.registry[4] = {5, "Firm 5", 0,      900, 0,  FirmStatus::OFFLINE, 0};
    hub.registry[5] = {6, "Firm 6", 250000, 220, 15, FirmStatus::FREE, 0};

    for(int i=0; i<6; i++) hub.registry[i].compute_fitness();
    hub.sort_registry();

    // STEP 1: Idle State
    hub.print_dashboard("SYSTEM IDLE - WAITING FOR SIGNALS");
    pause_ms(2000);

    // STEP 2: The Opportunity
    std::cout << MAGENTA << BOLD << "\n[EVENT DETECTED] Firm 1 detects Arbitrage Gap: Binance vs Bybit.\n" << RESET;
    std::cout << "  -> Gap Size: $3,000 profit.\n";
    std::cout << "  -> Expiration Window: ~500 microseconds.\n";
    pause_ms(1500);

    std::cout << RED << BOLD << "\n[RISK ENGINE REJECT] Firm 1 tries to execute but Capital is locked in settlement!\n" << RESET;
    pause_ms(1500);

    // STEP 3: Firm 1 Status Update
    for(int i=0; i<6; i++) {
        if(hub.registry[i].firm_id == 1) {
            hub.registry[i].status = FirmStatus::CAPITAL_EXHAUSTED;
            hub.registry[i].capital_usd = 0;
            hub.registry[i].compute_fitness();
        }
    }
    hub.sort_registry();
    hub.print_dashboard("FIRM 1 CAPACITY DROPPED - RESORTING MESH");
    pause_ms(2000);

    // STEP 4: SCP-L2 Delegation
    std::cout << CYAN << BOLD << "\n[SCP-L2 PROTOCOL INITIATED] Firm 1 delegates execution to AMOGH Hub.\n" << RESET;
    
    SCPFrame frame;
    frame.ethertype = 0x88B5;
    frame.msg_type = 1; // REAL
    frame.firm_id = 1;
    frame.seq_num = 1047;
    frame.prime_id = generate_prime(SHARED_SECRET, frame.seq_num);
    std::string payload_str = "EXEC_REQ:BTCUSDT:BUY:100:LIMIT";
    strncpy(frame.payload, payload_str.c_str(), sizeof(frame.payload));
    
    std::cout << DIM << "  +60ns  : Firm 1 DPDK NIC writes 88-byte raw ethernet frame.\n";
    pause_ms(500);
    std::cout << "  +120ns : Hub switch receives frame.\n";
    pause_ms(500);
    std::cout << "  +165ns : Hub CSPRNG prime verification -> " << GREEN << "SUCCESS [0x" << std::hex << frame.prime_id << std::dec << "]\n" << RESET;
    pause_ms(800);

    // STEP 5: Hub Routing
    int best_partner_id = hub.registry[0].firm_id;
    std::string best_partner_name = hub.registry[0].name;
    
    std::cout << YELLOW << BOLD << "\n[ROUTING] Hub querying L1 Cache for Rank 1 Partner...\n" << RESET;
    pause_ms(800);
    std::cout << GREEN << BOLD << "  +266ns : Match Found! Routing Layer-2 Frame to -> " << best_partner_name << " (Firm ID: " << best_partner_id << ")\n" << RESET;
    pause_ms(1000);

    // STEP 6: Execution & Ledger Split
    std::cout << MAGENTA << BOLD << "\n[TRADE EXECUTED] " << best_partner_name << " receives frame and executes via Bybit FIX API.\n" << RESET;
    pause_ms(1000);
    
    std::cout << CYAN << BOLD << "\n=====================================================================\n";
    std::cout << "  ATOMIC LEDGER :: PROFIT SPLIT (60% Signal / 40% Capital)\n";
    std::cout << "=====================================================================\n" << RESET;
    std::cout << GREEN << BOLD << "  [+] Total Arbitrage Profit : $3,000.00\n";
    std::cout << "  [+] Firm 1 (Signal Source) : $1,800.00\n";
    std::cout << "  [+] " << best_partner_name << " (Execution)   : $1,200.00\n" << RESET;
    std::cout << DIM << "\n  End-to-End AMOGH Delegation Time: ~700 nanoseconds.\n  Standard TCP/IP Equivalent Time: ~6,000 nanoseconds.\n" << RESET;
    std::cout << "\n";

    return 0;
}