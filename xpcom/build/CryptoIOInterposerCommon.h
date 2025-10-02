/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CryptoIOInterposerCommon_h
#define mozilla_CryptoIOInterposerCommon_h

#define CRYPTOIO_BLOCK_SIZE 128

/**
 * get the blocks to read/write, based on CRYPTOIO_BLOCK_SIZE
 *
 * returns first/last blocks (included) to read/write from
 *
 * First block is defined as the one that contains the offset position
 * Last block is definded as the one that contains the (offset + length - 1) position
 *   -> (length/offset - 1) because indexing starts at 0
 *
#define GET_BLOCKS(f, l, ef, el) { \
    printf("read %d@%d, expect to read from blocks (%d, %d)\n", f, l, ef, el); \
    size_t first, last; compute_blocks(f, l, &first, &last); \
    assert(first == ef); assert(last == el);
}

    //         length, offset, expected first, expected last
    GET_BLOCKS(1,      0,      0,              0);
    GET_BLOCKS(128,    0,      0,              0);
    GET_BLOCKS(128,    1,      0,              1);
    GET_BLOCKS(128,    1,      0,              1);
    GET_BLOCKS(128,    2,      0,              1);
    GET_BLOCKS(256,    1,      0,              2);
    GET_BLOCKS(256,    64,     0,              2);
    GET_BLOCKS(320,    64,     0,              2);
    GET_BLOCKS(320,    96,     0,              3);
    GET_BLOCKS(1,      127,    0,              0);
    GET_BLOCKS(1,      128,    1,              1);
    GET_BLOCKS(1,      127,    0,              0);
    GET_BLOCKS(2,      127,    0,              1);

 **/
void compute_blocks(size_t aLength, size_t aOffset, size_t* first, size_t* last)
{
    size_t firstBlock = std::floor((double)aOffset / (double)CRYPTOIO_BLOCK_SIZE);
    size_t lastBlock  = std::floor(((double)aOffset + (double)aLength - 1.0) / (double)CRYPTOIO_BLOCK_SIZE);

    *first = firstBlock;
    *last = lastBlock;
}

size_t blocks_access_size(size_t first, size_t last) {
  return ((last - first) + 1) * CRYPTOIO_BLOCK_SIZE;
}

#endif  // mozilla_CryptoIOInterposerCommon_h
