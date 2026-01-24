#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <fstream>
#include <algorithm>

#if defined(__linux__)
#include <sys/sysinfo.h>
#endif

// ----------------------------
// Simple CLI helpers
// ----------------------------
static bool has_arg(int argc, char** argv, const std::string& key) {
    for (int i = 1; i < argc; i++) {
        if (argv[i] == key) return true;
    }
    return false;
}

static std::string get_arg_value(int argc, char** argv, const std::string& key, const std::string& def = "") {
    for (int i = 1; i < argc - 1; i++) {
        if (argv[i] == key) return argv[i + 1];
    }
    return def;
}

static void usage(const char* prog) {
    std::cout <<
        "RAMTest CLI (v2)\n"
        "Usage:\n"
        "  " << prog << " --inventory\n"
        "  " << prog << " --test --mb <MB> --passes <N> [--pattern <aa|55|00|ff>] [--module <index>]\n\n"
        "Examples:\n"
        "  " << prog << " --inventory\n"
        "  " << prog << " --test --mb 64 --passes 2 --pattern aa\n"
        "  " << prog << " --test --mb 32 --passes 1 --pattern ff --module 1\n\n"
        "Notes:\n"
        "  - Inventory is best on Linux (real machine). Online sandboxes may not expose hardware info.\n"
        "  - Selecting a module is logical only: userland programs cannot force testing a specific physical DIMM.\n";
}

// ----------------------------
// RAM Inventory (best effort)
// ----------------------------
struct RamInventory {
    bool ok = false;
    uint64_t total_ram_bytes = 0;
    int modules_detected = 0;       // "populated slots" estimate
    std::string source;
};

static uint64_t parse_kb_line(const std::string& line) {
    // Example: "MemTotal:       16334256 kB"
    std::istringstream iss(line);
    std::string key;
    uint64_t kb = 0;
    std::string unit;
    iss >> key >> kb >> unit;
    return kb;
}

static RamInventory inventory_linux_meminfo() {
    RamInventory inv;
    std::ifstream f("/proc/meminfo");
    if (!f) {
        inv.ok = false;
        inv.source = "meminfo unavailable";
        return inv;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            uint64_t kb = parse_kb_line(line);
            inv.total_ram_bytes = kb * 1024ULL;
            inv.ok = true;
            inv.source = "/proc/meminfo";
            break;
        }
    }

    // We cannot reliably know "modules" from meminfo.
    // We'll leave it as unknown here (0) and explain.
    inv.modules_detected = 0;
    return inv;
}

static RamInventory inventory_linux_sysinfo() {
    RamInventory inv;
#if defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        inv.total_ram_bytes = static_cast<uint64_t>(si.totalram) * static_cast<uint64_t>(si.mem_unit);
        inv.ok = true;
        inv.source = "sysinfo()";
        inv.modules_detected = 0;
        return inv;
    }
#endif
    inv.ok = false;
    inv.source = "sysinfo unavailable";
    return inv;
}

static RamInventory get_inventory_best_effort() {
#if defined(__linux__)
    // Try sysinfo first, then /proc/meminfo
    RamInventory a = inventory_linux_sysinfo();
    if (a.ok) return a;

    RamInventory b = inventory_linux_meminfo();
    if (b.ok) return b;
#endif
    RamInventory inv;
    inv.ok = false;
    inv.source = "not supported on this platform";
    return inv;
}

static std::string human_bytes(uint64_t bytes) {
    const char* suffix[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        i++;
    }
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(2);
    oss << v << " " << suffix[i];
    return oss.str();
}

// ----------------------------
// RAM Test (userland)
// ----------------------------
static bool parse_pattern(const std::string& s, uint8_t& out) {
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);

    if (t == "aa") { out = 0xAA; return true; }
    if (t == "55") { out = 0x55; return true; }
    if (t == "00") { out = 0x00; return true; }
    if (t == "ff") { out = 0xFF; return true; }
    return false;
}

static int ram_test(size_t mb, int passes, uint8_t pattern) {
    const size_t bytes = mb * 1024ULL * 1024ULL;

    std::cout << "[*] Allocating " << mb << " MB (" << bytes << " bytes)\n";

    std::vector<uint8_t> buf;
    try {
        buf.resize(bytes);
    } catch (...) {
        std::cerr << "[!] Allocation failed. Try lower MB.\n";
        return 2;
    }

    size_t errors_total = 0;

    for (int p = 1; p <= passes; p++) {
        std::cout << "\n[*] Pass " << p << "/" << passes
                  << " | Pattern 0x" << std::hex << (int)pattern << std::dec << "\n";

        auto t0 = std::chrono::high_resolution_clock::now();
        std::memset(buf.data(), pattern, buf.size());
        auto t1 = std::chrono::high_resolution_clock::now();

        size_t errors = 0;
        for (size_t i = 0; i < buf.size(); i++) {
            if (buf[i] != pattern) {
                errors++;
                if (errors <= 5) {
                    std::cerr << "[!] Mismatch at offset " << i
                              << " got=0x" << std::hex << (int)buf[i]
                              << " expected=0x" << (int)pattern << std::dec << "\n";
                }
            }
        }
        auto t2 = std::chrono::high_resolution_clock::now();

        auto write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        auto verify_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

        double mb_written = static_cast<double>(mb);
        double write_sec = (write_ms / 1000.0);
        double verify_sec = (verify_ms / 1000.0);

        std::cout << "[*] Write time:  " << write_ms << " ms"
                  << " | ~" << (write_sec > 0 ? (mb_written / write_sec) : 0) << " MB/s\n";
        std::cout << "[*] Verify time: " << verify_ms << " ms"
                  << " | ~" << (verify_sec > 0 ? (mb_written / verify_sec) : 0) << " MB/s\n";
        std::cout << "[*] Errors this pass: " << errors << "\n";

        errors_total += errors;
    }

    std::cout << "\n[*] Done. Total errors: " << errors_total << "\n";
    return (errors_total == 0) ? 0 : 3;
}

// ----------------------------
// Main
// ----------------------------
int main(int argc, char** argv) {
    if (argc < 2 || has_arg(argc, argv, "--help") || has_arg(argc, argv, "-h")) {
        usage(argv[0]);
        return 1;
    }

    if (has_arg(argc, argv, "--inventory")) {
        RamInventory inv = get_inventory_best_effort();

        std::cout << "[*] RAM Inventory\n";
        std::cout << "    Source: " << inv.source << "\n";

        if (!inv.ok) {
            std::cout << "    Total RAM: unknown (sandbox/unsupported)\n";
            std::cout << "    Modules:   unknown\n";
            std::cout << "\n[!] Tip: on real Linux run: sudo dmidecode -t memory\n";
            return 0;
        }

        std::cout << "    Total RAM: " << human_bytes(inv.total_ram_bytes) << "\n";
        if (inv.modules_detected == 0) {
            std::cout << "    Modules:   unknown (needs dmidecode for DIMM slots)\n";
        } else {
            std::cout << "    Modules:   " << inv.modules_detected << "\n";
        }

        std::cout << "\n[!] Note: userland cannot map RAM pages to a specific DIMM reliably.\n";
        return 0;
    }

    if (has_arg(argc, argv, "--test")) {
        std::string mb_s = get_arg_value(argc, argv, "--mb", "");
        std::string passes_s = get_arg_value(argc, argv, "--passes", "1");
        std::string pattern_s = get_arg_value(argc, argv, "--pattern", "aa");
        std::string module_s = get_arg_value(argc, argv, "--module", "-1");

        if (mb_s.empty()) {
            std::cerr << "[!] Missing --mb <MB>\n";
            usage(argv[0]);
            return 1;
        }

        size_t mb = std::stoull(mb_s);
        int passes = std::stoi(passes_s);
        int module_index = std::stoi(module_s);

        uint8_t pattern = 0xAA;
        if (!parse_pattern(pattern_s, pattern)) {
            std::cerr << "[!] Invalid pattern: " << pattern_s << "\n";
            std::cerr << "    Valid: aa, 55, 00, ff\n";
            return 1;
        }

        if (mb == 0 || passes <= 0) {
            std::cerr << "[!] Invalid values: mb must be > 0, passes must be > 0\n";
            return 1;
        }

        std::cout << "[*] RAM Test Mode\n";
        if (module_index >= 0) {
            std::cout << "[*] Target module (logical): " << module_index << "\n";
            std::cout << "[!] Note: cannot force physical DIMM selection from userland.\n";
        }

        return ram_test(mb, passes, pattern);
    }

    std::cerr << "[!] Unknown mode.\n";
    usage(argv[0]);
    return 1;
}
