// ================================================================
//  AMOGH — CAPABILITY REGISTRY DEMO
//  The Order-Book Style Data Structure for Partner Firm Ranking
//
//  WHAT THIS DEMONSTRATES:
//  - 3 firms connected to the hub
//  - Each firm sends capability updates (capital, status, latency)
//  - Registry keeps them sorted by fitness score (best firm = top)
//  - Firm A detects a signal but is capital-exhausted → cannot trade
//  - Firm B is marked BUSY (actively trading something else)
//  - Firm C's last update shows it is FREE with capital available
//  - Registry instantly picks Firm C as the delegation target
//  - Signal sent. Trade delegated. Profit split logged.
//
//  COMPILE:
//  g++ -std=c++17 -O2 -o registry_demo registry_demo.cpp
//
//  RUN:
//  ./registry_demo
// ================================================================

#include <iostream>
#include <iomanip>
#include <string>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>

// ── Terminal colours for a clean demo output ─────────────────
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define MAGENTA "\033[35m"
#define DIM     "\033[2m"

// ── Pause helper ─────────────────────────────────────────────
void pause(int ms = 800) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void print_line(char c = '─', int len = 60) {
    std::cout << std::string(len, c) << "\n";
}


// ================================================================
//  FIRM STATUS — what state a firm is currently in
// ================================================================
enum class FirmStatus : uint8_t {
    FREE    = 0,   // available, can accept delegation
    BUSY    = 1,   // actively executing another trade
    CAPITAL = 2,   // capital exhausted, cannot trade
    OFFLINE = 3,   // not responding (heartbeat timeout)
};

const char* status_name(FirmStatus s) {
    switch(s) {
        case FirmStatus::FREE:    return "FREE";
        case FirmStatus::BUSY:    return "BUSY";
        case FirmStatus::CAPITAL: return "CAPITAL EXHAUSTED";
        case FirmStatus::OFFLINE: return "OFFLINE";
        default:                  return "UNKNOWN";
    }
}

const char* status_color(FirmStatus s) {
    switch(s) {
        case FirmStatus::FREE:    return GREEN;
        case FirmStatus::BUSY:    return YELLOW;
        case FirmStatus::CAPITAL: return RED;
        case FirmStatus::OFFLINE: return DIM;
        default:                  return RESET;
    }
}


// ================================================================
//  CAPABILITY ENTRY
//  One row in our registry — represents one connected firm
//  Updated every time a CAPABILITY message arrives from that firm
// ================================================================
struct CapabilityEntry {
    // ── Identity ─────────────────────────────────────────────
    uint32_t    firm_id;           // unique firm identifier
    char        firm_name[16];     // human readable name for demo

    // ── Current state (from latest CAPABILITY message) ───────
    uint64_t    capital_cents;     // free capital right now (in cents)
                                   // $500,000 = 50000000 cents
    uint32_t    venue_mask;        // which exchanges reachable
                                   // bit 0 = Binance, bit 1 = Bybit
    uint32_t    latency_us;        // self-reported execution latency
    uint32_t    order_slots;       // how many more orders can absorb
    FirmStatus  status;            // FREE / BUSY / CAPITAL / OFFLINE

    // ── Computed by hub ──────────────────────────────────────
    double      fitness_score;     // our ranking value — higher = better
    uint64_t    last_update_ms;    // when we last heard from this firm

    // ── Helper: is this firm eligible to receive delegation? ─
    bool is_eligible(uint32_t required_venue_bit) const {
        if (status != FirmStatus::FREE)  return false;
        if (capital_cents < 100 * 100)   return false;  // < $100
        if (order_slots == 0)             return false;
        if (!(venue_mask & required_venue_bit)) return false;
        return true;
    }
};


// ================================================================
//  FITNESS SCORE FORMULA
//
//  This is our algorithm — like a price formula in an order book
//  except instead of price we compute how good a partner this firm is
//
//  Weights:
//    40% — capital available  (most important, need money to trade)
//    25% — execution speed    (lower latency = higher score)
//    20% — order slot capacity (how many more orders can absorb)
//    10% — venue access       (do they have the right exchange?)
//     5% — recency penalty    (stale data = lower trust)
//
//  All inputs are normalised to 0.0–1.0 before weighting.
// ================================================================
double compute_fitness(
    const CapabilityEntry& e,
    uint64_t               max_capital,   // for normalisation
    uint32_t               max_latency,   // for normalisation
    uint32_t               required_venue_bit,
    uint64_t               now_ms
) {
    // ── If not eligible: fitness = 0, will sink to bottom ───
    if (!e.is_eligible(required_venue_bit)) return 0.0;

    // ── Normalise each factor to 0.0–1.0 ────────────────────
    double capital_score = (max_capital > 0)
        ? (double)e.capital_cents / (double)max_capital
        : 0.0;

    // Latency: lower is better, so we invert it
    // A firm with 100µs latency scores higher than one with 500µs
    double latency_score = (max_latency > 0 && e.latency_us > 0)
        ? (double)max_latency / (double)(e.latency_us * max_latency)
        : 0.0;
    latency_score = std::min(latency_score, 1.0);

    double slots_score = std::min((double)e.order_slots / 100.0, 1.0);

    double venue_score = (e.venue_mask & required_venue_bit) ? 1.0 : 0.0;

    // Recency: penalise stale data
    // Data older than 1000ms gets a 50% penalty
    uint64_t age_ms = now_ms - e.last_update_ms;
    double recency_score = std::max(0.0, 1.0 - (double)age_ms / 1000.0);

    // ── Weighted sum ─────────────────────────────────────────
    double fitness =
        (0.40 * capital_score)  +
        (0.25 * latency_score)  +
        (0.20 * slots_score)    +
        (0.10 * venue_score)    -
        (0.05 * (1.0 - recency_score));   // subtract staleness

    return std::max(0.0, fitness);
}


// ================================================================
//  CAPABILITY REGISTRY
//
//  Our core data structure.
//  Like an order book but for partner firms instead of price levels.
//
//  Order book:  sorted by price     → best price at index 0
//  Our registry: sorted by fitness  → best firm at index 0
//
//  Data structure: flat sorted array
//  Why not std::map or tree?
//    - We have 3–20 firms maximum, never thousands
//    - Flat array fits in L1 cache (20 firms = 960 bytes)
//    - Best firm lookup = array[0] = ONE nanosecond
//    - Re-sort 20 elements = ~100 nanoseconds
//    - A tree would be slower for this scale
// ================================================================
class CapabilityRegistry {
private:
    static constexpr int MAX_FIRMS = 32;
    CapabilityEntry entries[MAX_FIRMS];
    int             count = 0;

    uint64_t now_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    // Re-sort after every update (insertion sort — fast for tiny N)
    // Best fitness = highest score = goes to index 0 (top of registry)
    void sort_by_fitness() {
        std::sort(entries, entries + count, [](
            const CapabilityEntry& a,
            const CapabilityEntry& b) {
            return a.fitness_score > b.fitness_score; // descending
        });
    }

    uint64_t max_capital() const {
        uint64_t m = 1;
        for (int i = 0; i < count; i++)
            m = std::max(m, entries[i].capital_cents);
        return m;
    }

    uint32_t max_latency() const {
        uint32_t m = 1;
        for (int i = 0; i < count; i++)
            m = std::max(m, entries[i].latency_us);
        return m;
    }

public:
    // ── Add a new firm to the registry ───────────────────────
    void add_firm(uint32_t id, const char* name,
                  uint64_t capital, uint32_t venues,
                  uint32_t latency, uint32_t slots,
                  FirmStatus status) {
        if (count >= MAX_FIRMS) return;
        CapabilityEntry& e = entries[count++];
        e.firm_id       = id;
        strncpy(e.firm_name, name, 15);
        e.firm_name[15] = 0;
        e.capital_cents = capital;
        e.venue_mask    = venues;
        e.latency_us    = latency;
        e.order_slots   = slots;
        e.status        = status;
        e.last_update_ms= now_ms();
        e.fitness_score = 0.0;
        recalculate_all();
    }

    // ── Called when a CAPABILITY message arrives ─────────────
    // Updates the firm's state and re-sorts the registry
    void update_firm(uint32_t id, uint64_t capital,
                     uint32_t slots, FirmStatus status,
                     uint32_t required_venue = 0x02) {
        for (int i = 0; i < count; i++) {
            if (entries[i].firm_id == id) {
                entries[i].capital_cents  = capital;
                entries[i].order_slots    = slots;
                entries[i].status         = status;
                entries[i].last_update_ms = now_ms();
                recalculate_all();
                return;
            }
        }
    }

    // ── Recompute fitness for all firms + re-sort ─────────────
    void recalculate_all(uint32_t required_venue = 0x02) {
        uint64_t mc = max_capital();
        uint32_t ml = max_latency();
        uint64_t t  = now_ms();
        for (int i = 0; i < count; i++) {
            entries[i].fitness_score = compute_fitness(
                entries[i], mc, ml, required_venue, t);
        }
        sort_by_fitness();
    }

    // ── Get the best available partner ───────────────────────
    // Returns pointer to best firm, or nullptr if none available
    const CapabilityEntry* best_partner(uint32_t required_venue,
                                         uint32_t exclude_firm_id) const {
        for (int i = 0; i < count; i++) {
            if (entries[i].firm_id == exclude_firm_id) continue;
            if (entries[i].is_eligible(required_venue))
                return &entries[i];
        }
        return nullptr;
    }

    // ── Print the registry as a sorted table ─────────────────
    void print(const std::string& title = "CAPABILITY REGISTRY") const {
        std::cout << "\n" << BOLD << CYAN;
        print_line('═');
        std::cout << "  " << title << "\n";
        print_line('═');
        std::cout << RESET;

        // Header
        std::cout << BOLD
                  << std::left
                  << std::setw(6)  << "RANK"
                  << std::setw(10) << "FIRM"
                  << std::setw(14) << "CAPITAL"
                  << std::setw(10) << "LATENCY"
                  << std::setw(8)  << "SLOTS"
                  << std::setw(20) << "STATUS"
                  << std::setw(10) << "FITNESS"
                  << "\n" << RESET;
        print_line('─');

        for (int i = 0; i < count; i++) {
            const CapabilityEntry& e = entries[i];
            bool is_best = (i == 0 && e.fitness_score > 0.0);

            // Rank marker
            if (is_best)
                std::cout << GREEN << BOLD << "  ★ " << RESET;
            else
                std::cout << "  " << (i+1) << " ";

            // Firm name
            std::cout << std::left << std::setw(10) << e.firm_name;

            // Capital
            std::cout << "$" << std::setw(12)
                      << std::to_string(e.capital_cents / 100);

            // Latency
            std::cout << std::setw(10)
                      << (std::to_string(e.latency_us) + "µs");

            // Slots
            std::cout << std::setw(8) << e.order_slots;

            // Status with colour
            std::cout << status_color(e.status) << BOLD
                      << std::setw(20) << status_name(e.status)
                      << RESET;

            // Fitness score
            if (e.fitness_score > 0.0)
                std::cout << GREEN << std::fixed << std::setprecision(3)
                          << e.fitness_score << RESET;
            else
                std::cout << RED << "0.000 (ineligible)" << RESET;

            if (is_best)
                std::cout << GREEN << BOLD << "  ← BEST PARTNER" << RESET;

            std::cout << "\n";
        }
        print_line('─');
        std::cout << "\n";
    }

    int get_count() const { return count; }
};


// ================================================================
//  DEMO SCENARIO
//  Walks through the full delegation flow step by step
// ================================================================
void run_demo() {

    std::cout << BOLD << MAGENTA;
    print_line('═', 60);
    std::cout << "\n";
    std::cout << "  AMOGH — CAPABILITY REGISTRY DEMO\n";
    std::cout << "  Order-Book Style Partner Selection\n";
    std::cout << "\n";
    print_line('═', 60);
    std::cout << RESET << "\n";
    pause(500);

    // ── STEP 1: Create the registry ──────────────────────────
    std::cout << BOLD << "STEP 1: Hub starts. Three firms connect.\n" << RESET;
    std::cout << DIM << "  Each firm sends a HELLO + first CAPABILITY message.\n"
              << "  Hub adds them to the registry and computes fitness.\n" << RESET;
    pause(1000);

    CapabilityRegistry registry;

    // Firm 1 — decent capital, fast, Binance + Bybit access
    //   bit 0 = Binance (0x01), bit 1 = Bybit (0x02)
    registry.add_firm(
        1, "Firm-Alpha",
        50000000,   // $500,000 capital
        0x03,       // Binance + Bybit
        180,        // 180µs latency
        50,         // 50 order slots
        FirmStatus::FREE
    );

    // Firm 2 — less capital, slower, only Binance
    registry.add_firm(
        2, "Firm-Beta",
        12000000,   // $120,000 capital
        0x01,       // Binance only
        350,        // 350µs latency
        20,         // 20 order slots
        FirmStatus::FREE
    );

    // Firm 3 — medium capital, fast, Bybit access
    registry.add_firm(
        3, "Firm-Gamma",
        30000000,   // $300,000 capital
        0x03,       // Binance + Bybit
        220,        // 220µs latency
        35,         // 35 order slots
        FirmStatus::FREE
    );

    registry.recalculate_all(0x02);  // we need Bybit (bit 1)
    registry.print("INITIAL STATE — All Firms Connected and FREE");
    pause(1200);

    // ── STEP 2: Explain what the registry is ─────────────────
    std::cout << BOLD << CYAN
              << "  Think of it like an order book:\n" << RESET;
    std::cout << "  Order book  → sorted by PRICE    → best price at rank 1\n";
    std::cout << "  Our registry → sorted by FITNESS → best firm  at rank 1\n\n";
    std::cout << "  Fitness = 40% capital + 25% speed + 20% capacity"
              << " + 10% venue + recency\n\n";
    pause(1500);

    // ── STEP 3: Market event fires ────────────────────────────
    print_line();
    std::cout << BOLD << YELLOW
              << "\nSTEP 2: A price gap appears on the market!\n" << RESET;
    pause(600);
    std::cout << "\n";
    std::cout << "  Binance  →  BTC ask price:  $" << BOLD << "83,500" << RESET << "\n";
    std::cout << "  Bybit    →  BTC bid price:  $" << BOLD << GREEN << "83,530" << RESET << "\n";
    std::cout << "                              ──────\n";
    std::cout << "  Gap = $30 per coin × 100 coins = " << BOLD << GREEN
              << "$3,000 potential profit\n" << RESET;
    std::cout << "  Window = ~500,000 nanoseconds before gap closes\n\n";
    pause(1500);

    // ── STEP 4: Firm Alpha detects it but is exhausted ───────
    print_line();
    std::cout << BOLD << "\nSTEP 3: Firm-Alpha detects the gap...\n" << RESET;
    pause(800);
    std::cout << "  Signal engine fires: GAP DETECTED\n";
    std::cout << "  Risk engine checks: capital available...\n";
    pause(600);
    std::cout << RED << BOLD
              << "  ✗ CAPITAL EXHAUSTED — Firm-Alpha just used its capital\n"
              << "    on another trade that hasn't settled yet.\n"
              << RESET << "\n";
    std::cout << "  Firm-Alpha updates hub with new status...\n";
    pause(500);

    registry.update_firm(1, 500, 0, FirmStatus::CAPITAL, 0x02);
    registry.recalculate_all(0x02);
    registry.print("REGISTRY UPDATE — Firm-Alpha reports CAPITAL EXHAUSTED");
    pause(1200);

    // ── STEP 5: Firm Beta is busy ─────────────────────────────
    print_line();
    std::cout << BOLD << "\nSTEP 4: What about Firm-Beta?\n" << RESET;
    pause(600);
    std::cout << "  Hub checks Firm-Beta's last CAPABILITY update...\n";
    pause(500);
    std::cout << YELLOW << BOLD
              << "  ✗ BUSY — Firm-Beta is currently executing another trade.\n"
              << "    It sent a BUSY status update 200ms ago.\n"
              << RESET << "\n";
    std::cout << "  Hub marks Firm-Beta as unavailable...\n";
    pause(500);

    registry.update_firm(2, 12000000, 0, FirmStatus::BUSY, 0x02);
    registry.recalculate_all(0x02);
    registry.print("REGISTRY UPDATE — Firm-Beta reports BUSY");
    pause(1200);

    // ── STEP 6: Firm Gamma is free ────────────────────────────
    print_line();
    std::cout << BOLD << "\nSTEP 5: Firm-Gamma status check...\n" << RESET;
    pause(600);
    std::cout << "  Hub reads Firm-Gamma's latest CAPABILITY frame:\n";
    std::cout << "  → Last update: " << GREEN << "87ms ago" << RESET << " (fresh)\n";
    std::cout << "  → Capital: " << GREEN << "$300,000 available" << RESET << "\n";
    std::cout << "  → Status: " << GREEN << BOLD << "FREE" << RESET << "\n";
    std::cout << "  → Bybit access: " << GREEN << "YES (venue_mask bit 1 set)" << RESET << "\n";
    std::cout << "  → Order slots: " << GREEN << "35 remaining" << RESET << "\n\n";
    pause(1000);

    // ── STEP 7: Registry lookup ───────────────────────────────
    print_line();
    std::cout << BOLD << "\nSTEP 6: Hub queries registry for best partner...\n" << RESET;
    pause(800);
    std::cout << "  registry.best_partner(required_venue=BYBIT,"
              << " exclude=Firm-Alpha)\n";
    pause(500);
    std::cout << "  Scanning from index 0 (best fitness first)...\n";
    pause(400);
    std::cout << "  Index 0: Firm-Gamma → eligible? " << GREEN
              << BOLD << "YES ← SELECTED\n" << RESET;
    std::cout << "  " << DIM << "(Firm-Alpha and Firm-Beta already known ineligible)\n"
              << RESET;
    pause(800);

    const CapabilityEntry* best =
        registry.best_partner(0x02, 1);  // need Bybit, exclude Firm-Alpha

    std::cout << "\n";
    if (best) {
        std::cout << GREEN << BOLD;
        print_line('★', 60);
        std::cout << "\n  PARTNER SELECTED: " << best->firm_name << "\n";
        std::cout << "  Fitness score:    "
                  << std::fixed << std::setprecision(3)
                  << best->fitness_score << "\n";
        std::cout << "  Capital:          $"
                  << best->capital_cents / 100 << "\n";
        std::cout << "  Latency:          " << best->latency_us << " µs\n";
        std::cout << "  Lookup time:      ~1 nanosecond (index 0 of sorted array)\n";
        print_line('★', 60);
        std::cout << RESET << "\n";
    }
    pause(1200);

    // ── STEP 8: Signal sent ───────────────────────────────────
    print_line();
    std::cout << BOLD << "\nSTEP 7: EXEC_REQUEST sent to Firm-Gamma via SCP-L2\n"
              << RESET;
    pause(600);
    std::cout << "  Frame: 88 bytes, raw Ethernet, kernel bypass\n";
    std::cout << "  prime_tag: synchronized (clock-twinned prime)\n";
    std::cout << "  payload: {\n";
    std::cout << "    instrument: BTC/USDT\n";
    std::cout << "    side:       BUY\n";
    std::cout << "    quantity:   100 coins\n";
    std::cout << "    limit:      $83,530\n";
    std::cout << "    time_limit: 600 µs\n";
    std::cout << "    split:      40%% (Firm-Gamma keeps 40%%)\n";
    std::cout << "  }\n\n";
    pause(800);

    std::cout << "  Firm-Gamma receives it...\n";
    pause(400);
    std::cout << "  Risk engine auto-check: capital OK, within limits\n";
    pause(400);
    std::cout << GREEN << BOLD
              << "  ✓ Order routed to Bybit exchange!\n" << RESET;
    pause(400);
    std::cout << GREEN << BOLD
              << "  ✓ Firm-Alpha executes Binance leg simultaneously!\n"
              << RESET;
    pause(800);

    // ── STEP 9: Profit split ──────────────────────────────────
    print_line();
    std::cout << BOLD << "\nSTEP 8: Trade fills. Profit ledger updated.\n" << RESET;
    pause(600);
    std::cout << "\n";
    std::cout << "  Gross profit:      " << BOLD << "$3,000" << RESET << "\n";
    std::cout << "  Transaction costs: " << RED << "-$120" << RESET << "\n";
    std::cout << "  Net profit:        " << BOLD << GREEN << "$2,880" << RESET << "\n";
    std::cout << "\n";
    std::cout << "  Firm-Alpha (signal + Binance leg): "
              << GREEN << BOLD << "$1,728 (60%)" << RESET << "\n";
    std::cout << "  Firm-Gamma (capital + Bybit leg):  "
              << GREEN << BOLD << "$1,152 (40%)" << RESET << "\n";
    std::cout << "\n";
    std::cout << "  Ledger record: append-only, CRC32 verified\n";
    std::cout << "  Both firms can audit. Neither can alter it.\n\n";
    pause(1000);

    // ── STEP 10: Final registry state ─────────────────────────
    print_line();
    std::cout << BOLD << "\nSTEP 9: Registry updates after trade.\n" << RESET;
    pause(600);

    // After executing, Gamma's capital reduces, slots reduce
    registry.update_firm(3, 30000000 - (8353000 * 100), 34,
                         FirmStatus::FREE, 0x02);
    // Alpha's trade settling, gets capital back
    registry.update_firm(1, 50000000, 50, FirmStatus::FREE, 0x02);
    // Beta finishes its trade
    registry.update_firm(2, 12000000, 20, FirmStatus::FREE, 0x02);
    registry.recalculate_all(0x02);
    registry.print("FINAL STATE — After Trade Completion");
    pause(500);

    // ── Summary ───────────────────────────────────────────────
    std::cout << BOLD << MAGENTA;
    print_line('═', 60);
    std::cout << "\n  DEMO COMPLETE — KEY POINTS TO MENTION\n\n" << RESET;

    std::cout << BOLD << "  1. The data structure:\n" << RESET;
    std::cout << "     Flat sorted array. 3–20 firms. Fits in L1 cache.\n";
    std::cout << "     Same concept as order book — sorted by a score.\n";
    std::cout << "     Best partner always at index 0. Lookup = 1 nanosecond.\n\n";

    std::cout << BOLD << "  2. The fitness formula:\n" << RESET;
    std::cout << "     Capital (40%) + Speed (25%) + Capacity (20%)\n";
    std::cout << "     + Venue match (10%) − Staleness penalty (5%)\n\n";

    std::cout << BOLD << "  3. What happened:\n" << RESET;
    std::cout << "     Firm-Alpha: capital exhausted → fitness = 0 → sank to bottom\n";
    std::cout << "     Firm-Beta:  busy → fitness = 0 → sank to bottom\n";
    std::cout << "     Firm-Gamma: free + capital + right venue → rose to top\n";
    std::cout << "     Hub read registry[0] → Firm-Gamma selected in 1ns\n\n";

    std::cout << BOLD << "  4. Without AMOGH:\n" << RESET;
    std::cout << "     Firm-Alpha misses the trade. $3,000 profit = gone.\n\n";

    std::cout << BOLD << "  5. With AMOGH:\n" << RESET;
    std::cout << "     $2,880 split between two firms.\n";
    std::cout << "     Firm-Alpha earns $1,728 from a trade it couldn't do alone.\n\n";

    std::cout << MAGENTA << BOLD;
    print_line('═', 60);
    std::cout << RESET << "\n";
}


// ================================================================
//  MAIN
// ================================================================
int main() {
    run_demo();
    return 0;
}
