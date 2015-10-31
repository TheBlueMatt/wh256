/*
    Copyright (c) 2012-2015 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of WH256 nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include "wh256.h"

#include "cm256.h"
#include "wirehair_codec_8.hpp"

static bool m_init = false;

static const int WIREHAIR_THRESHOLD_N = 28;

int wh256_init_(int expected_version)
{
    // If version mismatch,
    if (expected_version != WH256_VERSION)
    {
        return -1;
    }

    if (gf256_init())
    {
        return -2;
    }
    if (cm256_init())
    {
        return -3;
    }
    m_init = true;

    return 0;
}


struct CodecState
{
    bool UsingWirehair;
    wirehair::Codec* WirehairCodec;

    // CM256 state:
    cm256_encoder_params EncoderParams;
    const uint8_t* OriginalMessage;
    cm256_block_t Blocks[256];
    uint8_t* LastBlock;
    int BlocksReceived;
    int LastBlockSize;

    void ResetCM256()
    {
        // In encoder mode,
        if (OriginalMessage)
        {
            delete[] LastBlock;
            LastBlock = nullptr;
        }
        else
        {
            for (int i = 0; i < 256; ++i)
            {
                delete[] (uint8_t*)Blocks[i].Block;
                Blocks[i].Block = nullptr;
            }
        }
        BlocksReceived = 0;
        LastBlockSize = 0;
    }

    CodecState()
    {
        for (int i = 0; i < 256; ++i)
        {
            Blocks[i].Block = nullptr;
        }
        WirehairCodec = nullptr;
        LastBlock = nullptr;
    }
    ~CodecState()
    {
        delete WirehairCodec;

        ResetCM256();
    }
};


wh256_state wh256_encoder_init(wh256_state reuse_E, const void* message, int bytes, int block_bytes)
{
    // If input is invalid,
    if (!m_init || !message || bytes < 1 || block_bytes < 1)
    {
        return 0;
    }

    CodecState* codec = reinterpret_cast<CodecState*>(reuse_E);

    // Allocate a new Codec object
    if (!codec)
    {
        codec = new CodecState;
    }

    // Use CM256 up to 128 input blocks
    int N = (bytes + block_bytes - 1) / block_bytes;
    codec->UsingWirehair = (N >= WIREHAIR_THRESHOLD_N);

    if (!codec->UsingWirehair)
    {
        codec->ResetCM256();
        codec->OriginalMessage = static_cast<const uint8_t*>(message);

        codec->EncoderParams.OriginalCount = N;
        codec->EncoderParams.RecoveryCount = 256 - N;
        codec->EncoderParams.BlockBytes = block_bytes;

        const uint8_t* block = codec->OriginalMessage;
        for (int i = 0; i < N - 1; ++i, block += block_bytes)
        {
            codec->Blocks[i].Block = (void*)block;
        }

        delete[] codec->LastBlock;
        codec->LastBlock = new uint8_t[block_bytes];

        int remainder = bytes % block_bytes;
        if (remainder <= 0)
        {
            remainder = block_bytes;
        }

        memcpy(codec->LastBlock, block, remainder);
        memset(codec->LastBlock + remainder, 0, block_bytes - remainder);

        codec->Blocks[N - 1].Block = codec->LastBlock;
    }
    else
    {
        if (!codec->WirehairCodec)
        {
            codec->WirehairCodec = new wirehair::Codec;
        }

        // Initialize codec
        wirehair::Result r = codec->WirehairCodec->InitializeEncoder(bytes, block_bytes);

        if (!r)
        {
            // Feed message to codec
            r = codec->WirehairCodec->EncodeFeed(message);
        }

        // On failure,
        if (r)
        {
            assert(false);
            delete codec;
            codec = 0;
        }
    }

    return codec;
}

int wh256_count(wh256_state E)
{
    // If input is invalid,
    if (!E)
    {
        return 0;
    }

    CodecState* codec = reinterpret_cast<CodecState*>(E);

    if (codec->UsingWirehair)
    {
        return codec->WirehairCodec->BlockCount();
    }
    else
    {
        return codec->EncoderParams.OriginalCount;
    }
}

int wh256_encoder_write(wh256_state E, unsigned int id, void* block)
{
    // If input is invalid,
    if (!E || !block)
    {
        return 0;
    }

    CodecState* codec = reinterpret_cast<CodecState*>(E);

    if (codec->UsingWirehair)
    {
        return codec->WirehairCodec->Encode(id, block) > 0 ? 0 : -1;
    }

    // CM256:
    if (id < static_cast<unsigned int>(codec->EncoderParams.OriginalCount))
    {
        memcpy(block, codec->Blocks[id].Block, codec->EncoderParams.BlockBytes);
    }
    else
    {
        if (id >= static_cast<unsigned int>(codec->EncoderParams.OriginalCount))
        {
            id = ((id - codec->EncoderParams.OriginalCount) % codec->EncoderParams.RecoveryCount) + codec->EncoderParams.OriginalCount;
        }

        cm256_encode_block(codec->EncoderParams, codec->Blocks, id, block);
    }

    return 0;
}

wh256_state wh256_decoder_init(wh256_state reuse_E, int bytes, int block_bytes)
{
    // If input is invalid,
    if (bytes < 1 || block_bytes < 1)
    {
        return 0;
    }

    CodecState* codec = reinterpret_cast<CodecState*>(reuse_E);

    // Allocate a new Codec object
    if (!codec)
    {
        codec = new CodecState;
    }

    // Use CM256 up to 128 input blocks
    int N = (bytes + block_bytes - 1) / block_bytes;
    codec->UsingWirehair = (N >= WIREHAIR_THRESHOLD_N);

    if (codec->UsingWirehair)
    {
        if (!codec->WirehairCodec)
        {
            codec->WirehairCodec = new wirehair::Codec;
        }

        // Allocate memory for decoding
        wirehair::Result r = codec->WirehairCodec->InitializeDecoder(bytes, block_bytes);

        if (r)
        {
            assert(false);
            delete codec;
            codec = 0;
        }
    }
    else
    {
        codec->ResetCM256();

        codec->OriginalMessage = nullptr;

        codec->EncoderParams.BlockBytes = block_bytes;
        codec->EncoderParams.OriginalCount = N;
        codec->EncoderParams.RecoveryCount = 256 - N;

        codec->LastBlockSize = bytes - N * block_bytes;
        if (codec->LastBlockSize <= 0)
        {
            codec->LastBlockSize = block_bytes;
        }

        for (int i = 0; i < N; ++i)
        {
            codec->Blocks[i].Block = new uint8_t[block_bytes];
        }
    }

    return codec;
}

int wh256_decoder_read(wh256_state E, unsigned int id, const void *block)
{
    // If input is invalid,
    if (!E || !block)
    {
        return 0;
    }

    CodecState* codec = reinterpret_cast<CodecState*>(E);

    if (codec->UsingWirehair)
    {
        if (wirehair::R_WIN == codec->WirehairCodec->DecodeFeed(id, block))
        {
            return 0;
        }

        return -1;
    }

    if (id >= static_cast<unsigned int>(codec->EncoderParams.OriginalCount))
    {
        id = ((id - codec->EncoderParams.OriginalCount) % codec->EncoderParams.RecoveryCount) + codec->EncoderParams.OriginalCount;
    }

    codec->Blocks[codec->BlocksReceived].Index = id;
    memcpy(codec->Blocks[codec->BlocksReceived].Block, block, codec->EncoderParams.BlockBytes);

    if (++codec->BlocksReceived == codec->EncoderParams.OriginalCount)
    {
        if (0 == cm256_decode(codec->EncoderParams, codec->Blocks))
        {
            return 0;
        }
        else
        {
            assert(false);
            codec->BlocksReceived = 0;
        }
    }

    return -1;
}

int wh256_decoder_reconstruct(wh256_state E, void *message)
{
    // If input is invalid,
    if (!E || !message)
    {
        return -1;
    }

    CodecState* codec = reinterpret_cast<CodecState*>(E);

    if (codec->UsingWirehair)
    {
        if (wirehair::R_WIN == codec->WirehairCodec->ReconstructOutput(message))
        {
            return 0;
        }

        return -1;
    }

    if (codec->BlocksReceived < codec->EncoderParams.OriginalCount)
    {
        return -1;
    }

    uint8_t* blocksOut = (uint8_t*)message;
    for (int i = 0; i < codec->EncoderParams.OriginalCount; ++i)
    {
        int index = codec->Blocks[i].Index;
        if (index >= codec->EncoderParams.OriginalCount)
        {
            assert(false);
            return -1;
        }
        if (index == codec->EncoderParams.OriginalCount - 1)
        {
            memcpy(blocksOut + index * codec->EncoderParams.BlockBytes, codec->Blocks[i].Block, codec->LastBlockSize);
        }
        else
        {
            memcpy(blocksOut + index * codec->EncoderParams.BlockBytes, codec->Blocks[i].Block, codec->EncoderParams.BlockBytes);
        }
    }

    return 0;
}

int wh256_decoder_reconstruct_block(wh256_state E, unsigned int id, void *block)
{
    // If input is invalid,
    if (!E || !block)
    {
        return -1;
    }

    CodecState* codec = reinterpret_cast<CodecState*>(E);

    if (codec->UsingWirehair)
    {
        if (wirehair::R_WIN == codec->WirehairCodec->ReconstructBlock(id, block))
        {
            return 0;
        }

        return -1;
    }

    if (codec->BlocksReceived < codec->EncoderParams.OriginalCount)
    {
        return -1;
    }

    for (int i = 0; i < codec->EncoderParams.OriginalCount; ++i)
    {
        int index = codec->Blocks[i].Index;
        if (index != id)
        {
            continue;
        }

        if (index == codec->EncoderParams.OriginalCount - 1)
        {
            memcpy(block, codec->Blocks[i].Block, codec->LastBlockSize);
        }
        else
        {
            memcpy(block, codec->Blocks[i].Block, codec->EncoderParams.BlockBytes);
        }
        return 0;
    }

    return -1;
}

void wh256_free(wh256_state E)
{
    CodecState* codec = reinterpret_cast<CodecState*>(E);

    delete codec;
}
