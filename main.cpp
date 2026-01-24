#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstring>

static void usage(const char* prog) {
    std::cout << "Usage: " << prog << " <MB> [passes]\n"
              << "Example: " << prog << " 512 3\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    size_t mb = std::stoull(argv[1]);
    int passes = (argc >= 3) ? std::stoi(argv[2]) : 1;

    size_t bytes = mb * 1024ULL * 1024ULL;
    std::cout << "[*] Allocating " << mb << " MB (" << bytes << " bytes)\n";

    std::vector<uint8_t> buf;
    try {
        buf.resize(bytes);
    } catch (...) {
        std::cerr << "[!] Allocation failed. Try lower MB.\n";
        return 2;
    }

    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

    size_t errors_total = 0;

    for (int p = 1; p <= passes; p++) {
        std::cout << "\n[*] Pass " << p << "/" << passes << "\n";

        // Pattern random per blocchi da 8 bytes
        auto start_write = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i + 8 <= buf.size(); i += 8) {
            uint64_t v = dist(rng);
            std::memcpy(&buf[i], &v, 8);
        }
        auto end_write = std::chrono::high_resolution_clock::now();

        // Verify: rigenero stessi random? no (qui controllo coerenza interna semplice)
        // -> per fare verify "vero", dovresti salvare i pattern oppure usare pattern fissi.
        // Qui facciamo un test con pattern fisso dopo quello random.

        // Pattern fisso 0xAA
        std::memset(buf.data(), 0xAA, buf.size());

        auto start_verify = std::chrono::high_resolution_clock::now();
        size_t errors = 0;
        for (size_t i = 0; i < buf.size(); i++) {
            if (buf[i] != 0xAA) {
                errors++;
                if (errors < 10) {
                    std::cerr << "[!] Mismatch at offset " << i
                              << " got=0x" << std::hex << (int)buf[i]
                              << " expected=0xAA" << std::dec << "\n";
                }
            }
        }
        auto end_verify = std::chrono::high_resolution_clock::now();

        errors_total += errors;

        auto write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_write - start_write).count();
        auto verify_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_verify - start_verify).count();

        std::cout << "[*] Write time:  " << write_ms << " ms\n";
        std::cout << "[*] Verify time: " << verify_ms << " ms\n";
        std::cout << "[*] Errors this pass: " << errors << "\n";
    }

    std::cout << "\n[*] Done. Total errors: " << errors_total << "\n";
    return (errors_total == 0) ? 0 : 3;
}
