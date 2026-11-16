// Checks the Zing MP3 request signing in src/assistant/assistant_zing.cc against vectors produced
// by research/zing/zing_proto.py (`python3 zing_proto.py --sig-vectors`). The device code hashes
// with mbedtls; here a small portable SHA-256 / SHA-512 / HMAC stands in, so no libraries are
// needed. Only the ESP-independent part of assistant_zing.cc is compiled (ZING_SIG_HOST_TEST).
#define ZING_SIG_HOST_TEST
#include "../src/assistant/assistant_zing.cc"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

uint32_t Rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
uint64_t Rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

std::string Hex(const uint8_t* d, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += h[d[i] >> 4];
        s += h[d[i] & 15];
    }
    return s;
}

std::string Sha256(const std::string& msg) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string m = msg;
    uint64_t bits = (uint64_t)msg.size() * 8;
    m += (char)0x80;
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 7; i >= 0; i--) m += (char)(bits >> (i * 8));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            const auto* p = reinterpret_cast<const uint8_t*>(m.data() + off + i * 4);
            w[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25)) + ((e & f) ^ (~e & g)) +
                          k[i] + w[i];
            uint32_t t2 = (Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    uint8_t out[32];
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(h[i] >> (24 - j * 8));
    return std::string(reinterpret_cast<char*>(out), 32);
}

std::string Sha512(const std::string& msg) {
    static const uint64_t k[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
    uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                     0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                     0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    std::string m = msg;
    uint64_t bits = (uint64_t)msg.size() * 8;
    m += (char)0x80;
    while (m.size() % 128 != 112) m += (char)0;
    for (int i = 0; i < 8; i++) m += (char)0;  // high 64 bits of the 128-bit length
    for (int i = 7; i >= 0; i--) m += (char)(bits >> (i * 8));
    for (size_t off = 0; off < m.size(); off += 128) {
        uint64_t w[80];
        for (int i = 0; i < 16; i++) {
            const auto* p = reinterpret_cast<const uint8_t*>(m.data() + off + i * 8);
            w[i] = 0;
            for (int j = 0; j < 8; j++) w[i] = (w[i] << 8) | p[j];
        }
        for (int i = 16; i < 80; i++) {
            uint64_t s0 = Rotr64(w[i - 15], 1) ^ Rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = Rotr64(w[i - 2], 19) ^ Rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; i++) {
            uint64_t t1 = hh + (Rotr64(e, 14) ^ Rotr64(e, 18) ^ Rotr64(e, 41)) +
                          ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint64_t t2 = (Rotr64(a, 28) ^ Rotr64(a, 34) ^ Rotr64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    uint8_t out[64];
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) out[i * 8 + j] = (uint8_t)(h[i] >> (56 - j * 8));
    return std::string(reinterpret_cast<char*>(out), 64);
}

}  // namespace

namespace assistant::zing_internal {

std::string Sha256Hex(const std::string& data) {
    std::string d = Sha256(data);
    return Hex(reinterpret_cast<const uint8_t*>(d.data()), d.size());
}

std::string HmacSha512Hex(const std::string& key, const std::string& msg) {
    std::string k = key.size() > 128 ? Sha512(key) : key;
    k.resize(128, '\0');
    std::string ipad(128, '\0'), opad(128, '\0');
    for (int i = 0; i < 128; i++) {
        ipad[i] = (char)(k[i] ^ 0x36);
        opad[i] = (char)(k[i] ^ 0x5c);
    }
    std::string d = Sha512(opad + Sha512(ipad + msg));
    return Hex(reinterpret_cast<const uint8_t*>(d.data()), d.size());
}

}  // namespace assistant::zing_internal

int fails = 0;

void Check(const char* name, const std::string& got, const std::string& want) {
    bool ok = got == want;
    printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok) {
        printf("     got  %s\n     want %s\n", got.c_str(), want.c_str());
        fails++;
    }
}

int main() {
    using namespace assistant::zing_internal;
    // Hash primitives (FIPS 180 / RFC 4231 test case 2).
    Check("sha256(abc)", Sha256Hex("abc"),
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Check("hmac-sha512 rfc4231#2", HmacSha512Hex("Jefe", "what do ya want for nothing?"),
          "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fd"
          "caeab1a34d4a6b4b636e070a38bce737");

    Check("encodeURIComponent", UriEncode("Hà Nội mùa thu (1)!"),
          "H%C3%A0%20N%E1%BB%99i%20m%C3%B9a%20thu%20(1)!");

    // Vectors from `python3 research/zing/zing_proto.py --sig-vectors`.
    Params stream = {{"id", "ZWZB969E"}, {"ctime", "1700000000"}, {"version", "1.22.4"}};
    Check("param string", SigParamString(stream), "ctime=1700000000id=ZWZB969Eversion=1.22.4");
    Check("sha256(param string)", Sha256Hex(SigParamString(stream)),
          "1ebc1389b4bf74ad1517e29f4e254f7bf332a04a9ba3ce75ad1d64a296ca58b4");
    Check("sig song/get/streaming", Signature("/api/v2/song/get/streaming", stream),
          "c9f694dd60e0bbe8a4ae72f0661a6d6cdbe7d022b72c3bec8bcde9dafd2ff690"
          "d5d529d1c04b35c1db0c0f513ca874f4f0170a29c52763a99866c7a22effa7f4");

    Params search = {{"q", "Hà Nội mùa thu"}, {"type", "song"}, {"page", "1"},
                     {"count", "10"}, {"ctime", "1700000000"}, {"version", "1.22.4"}};
    Check("sig search (q not signed)", Signature("/api/v2/search", search),
          "643770c618a84f12441880d8a27f2c119edb74fef6cec0aa094bf2266da4fd60"
          "ca8fa993376d31d20edd925350f335cf0415761105dbdc3cbe933101d9c576c2");

    Params multi = {{"ctime", "1700000000"}, {"version", "1.22.4"}, {"q", "Sơn Tùng"}};
    Check("sig search/multi", Signature("/api/v2/search/multi", multi),
          "781f324d3dd8fe3268dcb7cf33d5932ef506bec220217d89a76ae911a236117f"
          "4eb10e8f7d86ee541008904c6bba2a305750e3ea66e80f988d435b0936456255");
    return fails;
}
