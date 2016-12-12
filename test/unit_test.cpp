#include "../src/wh256.h"

#include "Clock.hpp"
#include "AbyssinianPRNG.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cassert>
#include <stdint.h>
using namespace std;
using namespace cat;

static Clock m_clock;


// Simulation seed
const uint32_t SEED = 1;

// Number of trials to run
const int TRIALS = 1000;

#include <vector>
std::vector<int> ExceptionList;

void GenTable()
{
    ofstream file;
    file.open("except_table.txt");

    const int words = (64000 + 63) / 64;
    uint64_t table[words] = { 0 };

    for (int ii = 0; ii < (int)ExceptionList.size(); ++ii)
    {
        uint32_t n = ExceptionList[ii];

        table[n >> 6] |= (uint64_t)1 << (n & 63);
    }

    {
        cout << "static const uint64_t EXCEPT_TABLE[" << words << "] = {";
        for (int ii = 0; ii < words; ++ii)
        {
            if ((ii & 7) == 0) cout << endl;
            cout << hex << "0x" << setfill('0') << table[ii] << dec << "ULL, ";
        }
        cout << endl << "};" << endl;
    }
    {
        file << "static const uint64_t EXCEPT_TABLE[" << words << "] = {";
        for (int ii = 0; ii < words; ++ii)
        {
            if ((ii & 7) == 0) file << endl;
            file << hex << "0x" << setfill('0') << table[ii] << dec << "ULL, ";
        }
        file << endl << "};" << endl;
    }
}


static void TestBlockSizes()
{
    const int MaxBlockSize = 128;
    uint8_t block[MaxBlockSize];

    wh256_state encoder = 0, decoder = 0;
    Abyssinian prng;

    prng.Initialize(m_clock.msec(), Clock::cycles());

    static const int NValues[] = {
        1, 2, 3, 4, 5, 28, 29, 30, 31, 32, 128, 1037
    };

    for (int block_bytes = 1; block_bytes <= MaxBlockSize; ++block_bytes)
    {
        // Try some values for N
        for (int Nindex = 0; Nindex < (int)(sizeof(NValues) / sizeof(*NValues)); ++Nindex)
        {
            const int N = NValues[Nindex];

            const int bytes = block_bytes * N;
            uint8_t *message_in = new uint8_t[bytes];
            uint8_t *message_out = new uint8_t[bytes];

            prng.Initialize(SEED);

            // Fill input message with random data
            for (int ii = 0; ii < bytes; ++ii)
            {
                message_in[ii] = (uint8_t)prng.Next();
            }

            double t0 = m_clock.usec();

            // Initialize encoder
            encoder = wh256_encoder_init(encoder, message_in, bytes, block_bytes);
            if (!encoder)
            {
                cout << "*** Block size test failed during encoder init for N=" << N << " and block_bytes=" << block_bytes << endl;
                assert(false);
                continue;
            }
#if 1
            assert(encoder);
            double t1 = m_clock.usec();

            assert(N == wh256_count(encoder));

            // Initialize decoder
            decoder = wh256_decoder_init(decoder, bytes, block_bytes);
            assert(decoder);

            assert(N == wh256_count(decoder));

            // Simulate transmission
            for (uint32_t id = 0;; ++id)
            {
                // 50% packetloss to randomize received message IDs
                if (prng.Next() % 100 < 50)
                {
                    continue;
                }

                // Write a block
                int bytes_written;
                int writeResult = wh256_encoder_write(encoder, id, block, &bytes_written);
                assert(0 == writeResult);

                // If decoder is ready:
                if (0 == wh256_decoder_read(decoder, id, block))
                {
                    // If message is decoded:
                    if (0 == wh256_decoder_reconstruct(decoder, message_out))
                    {
                        if (memcmp(message_in, message_out, bytes))
                        {
                            cout << "*** Decode failure for N=" << N << " and block_bytes=" << block_bytes << endl;
                            assert(false);
                        }

                        // Done with transmission simulation
                        break;
                    }
                }
            }

            //cout << "Verified N=" << N << " and block_bytes=" << block_bytes << endl;

#endif
            delete[]message_in;
            delete[]message_out;
        }
    }

    cout << "Verified that different block sizes all work" << endl;
}


//// Entrypoint

int main()
{
    if (wirehair_init())
    {
        exit(1);
    }

    m_clock.OnInitialize();

    //TestBlockSizes();

    wh256_state encoder = 0, decoder = 0;
    Abyssinian prng;

    // Simulate file transfers over UDP/IP
    const int block_bytes = 1;
    uint8_t block[block_bytes];

    prng.Initialize(m_clock.msec(), Clock::cycles());

#if 0

    uint8_t heat_map[256 * 256] = { 0 };

    for (int N = 2; N < 256; ++N) {
        int bytes = block_bytes * N;
        uint8_t *message_in = new uint8_t[bytes];

        // Fill input message with random data
        for (int ii = 0; ii < bytes; ++ii) {
            message_in[ii] = (uint8_t)prng.Next();
        }

        double sum = 0;
        for (int trials = 0; trials < 10; ++trials) {
            double t0 = m_clock.usec();

            // Initialize encoder
            encoder = wh256_encode(encoder, message_in, bytes, block_bytes);
            assert(encoder);

            double t1 = m_clock.usec();
            sum += t1 - t0;
        }
        double encode_time = sum / 10.;

        for (uint32_t count = 0; count < 256; ++count) {
            double t0 = m_clock.usec();
            for (int ii = 0; ii < count; ++ii) {
                assert(wh256_write(encoder, ii, block));
            }
            double t1 = m_clock.usec();
            double write_time = t1 - t0;

            double overall = encode_time + write_time;
            int speed = block_bytes * N / overall;

            uint8_t map_value = 0;

            if (speed < 10) {
                map_value = 1;
            } else if (speed < 50) {
                map_value = 2;
            } else if (speed < 100) {
                map_value = 3;
            } else if (speed < 200) {
                map_value = 4;
            } else if (speed < 300) {
                map_value = 5;
            } else if (speed < 400) {
                map_value = 6;
            } else if (speed < 500) {
                map_value = 7;
            } else {
                map_value = 8;
            }

            heat_map[N * 256 + count] = map_value;
        }

        assert(N == wh256_count(encoder));

        delete []message_in;
    }

    ofstream file;
    file.open("heatmap.txt");

    for (int ii = 0; ii < 256; ++ii) {
        file << ii << ", ";
        for (int jj = 0; jj < 256; ++jj) {
            uint8_t map_value = heat_map[ii * 256 + jj];

            file << (int)map_value << ",";
        }
        file << endl;
    }

#endif

    // Try each value for N
    for (int N = 1; N <= 64000; ++N)
    {
        int bytes = block_bytes * N;
        uint8_t *message_in = new uint8_t[bytes];
        uint8_t *message_out = new uint8_t[bytes];

        prng.Initialize(SEED);

        // Fill input message with random data
        for (int ii = 0; ii < bytes; ++ii) {
            message_in[ii] = (uint8_t)prng.Next();
        }

        double t0 = m_clock.usec();

        // Initialize encoder
        encoder = wh256_encoder_init(encoder, message_in, bytes, block_bytes);
        if (!encoder)
        {
            cout << "*** Seed failed the first time! " << N << endl;
            ExceptionList.push_back(N);
            continue;
        }
#if 1
        assert(encoder);
        double t1 = m_clock.usec();

        assert(N == wh256_count(encoder));

        double encode_time_base = t1 - t0;
        double encode_time_extra = 0.;

        // For each trial:
        uint32_t overhead = 0;
        double reconstruct_time = 0;
        int sumLosses = 0;
        bool decoderReady = false;
        for (int ii = 0; ii < TRIALS; ++ii)
        {
            // Initialize decoder
            decoder = wh256_decoder_init(decoder, bytes, block_bytes);
            assert(decoder);

            assert(N == wh256_count(decoder));

            //N=2402
            //prng._x = 10606512583085954776;
            //prng._y = 17769964918691237524;

            // Simulate transmission
            int blocks_needed = 0;
            for (uint32_t id = 0;; ++id)
            {
                // 50% packetloss to randomize received message IDs
                if (prng.Next() % 100 < 50)
                {
                    ++sumLosses;
                    continue;
                }

                // Add a block
                ++blocks_needed;

                // Write a block
                t0 = m_clock.usec();
                int bytes_written;
                int writeResult = wh256_encoder_write(encoder, id, block, &bytes_written);
                t1 = m_clock.usec();
                encode_time_extra += t1 - t0;
                assert(0 == writeResult);

                // If decoder is ready:
                t0 = m_clock.usec();
                if (0 == wh256_decoder_read(decoder, id, block))
                {
                    // If message is decoded:
                    if (0 == wh256_decoder_reconstruct(decoder, message_out))
                    {
                        t1 = m_clock.usec();
                        reconstruct_time += t1 - t0;

                        if (memcmp(message_in, message_out, bytes))
                        {
                            cout << "*** Decode failure at " << N << endl;
                            assert(false);
                        }

                        // Done with transmission simulation
                        decoderReady = true;
                        break;
                    }
                }

                if (blocks_needed >= N + 64)
                {
                    cout << "*** SEED NEEDS TO BE FIXED FOR " << N << " *** Needed a ton of blocks" << endl;
                    ExceptionList.push_back(N);
                    decoderReady = false;
                    assert(false);
                    break;
                }
            }
            overhead += blocks_needed - N;
        }

        double overhead_avg = overhead / (double)TRIALS;
        double reconstruct_avg = reconstruct_time / TRIALS;
        double encode_time = (encode_time_base + encode_time_extra / TRIALS);

        cout << ">> wirehair_encode(N = " << N << ") in " << encode_time << " usec, " << bytes / encode_time << " MB/s after " << (sumLosses / (double)TRIALS) << " avg losses" << endl;
        cout << "<< wirehair_decode(N = " << N << ") average overhead = " << overhead_avg << " blocks, average reconstruct time = " << reconstruct_avg << " usec, " << bytes / reconstruct_avg << " MB/s" << endl;

        if (overhead_avg > 0.04)
        {
            cout << "*** SEED NEEDS TO BE FIXED FOR " << N << " *** " << overhead_avg << endl;
            ExceptionList.push_back(N);
        }

        // Verify that decoder can become encoder:

        if (!decoderReady)
        {
            cout << "*** Skipping decoder transmogrification due to previous failure at " << N << endl;
        }
        else if (0 != wh256_decoder_becomes_encoder(decoder))
        {
            cout << "*** Decoder cannot be transmogrified into encoder at " << N << endl;
            assert(false);
        }
        else
        {
            wh256_state decoder_now_encoder = decoder;
            assert(N == wh256_count(decoder_now_encoder));

            // Initialize decoder
            wh256_state decoder2 = wh256_decoder_init(nullptr, bytes, block_bytes);
            assert(decoder2);

            // Simulate transmission with reusing the decoder as encoder
            for (uint32_t id2 = 0;; ++id2)
            {
                // 50% packetloss to randomize received message IDs
                if (prng.Next() % 100 < 50)
                {
                    continue;
                }

                // Write a block
                int bytes_written;
                int writeResult = wh256_encoder_write(decoder_now_encoder, id2, block, &bytes_written);
                assert(0 == writeResult);

                // If decoder is ready:
                if (0 == wh256_decoder_read(decoder2, id2, block))
                {
                    // If message is decoded:
                    if (0 == wh256_decoder_reconstruct(decoder2, message_out))
                    {
                        if (memcmp(message_in, message_out, bytes))
                        {
                             cout << "*** While using decoder as encoder: Decode failure at " << N << endl;
                            assert(false);
                        }

                        break;
                    }
                }
            }
        }

        //cout << endl;
#endif
        delete []message_in;
        delete []message_out;
    }

    GenTable();

    m_clock.OnFinalize();

    wh256_free(encoder);
    wh256_free(decoder);

    return 0;
}
