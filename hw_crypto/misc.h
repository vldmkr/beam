#pragma once

#include <memory.h>

#include "multimac.h"
#include "oracle.h"
#include "noncegen.h"
#include "coinid.h"
#include "kdf.h"
#include "rangeproof.h"
#include "sign.h"
#include "keykeeper.h"

#include "secp256k1-zkp/src/group_impl.h"
#include "secp256k1-zkp/src/scalar_impl.h"
#include "secp256k1-zkp/src/field_impl.h"
#include "secp256k1-zkp/src/hash_impl.h"

typedef struct
{
  BeamCrypto_CoinID coins[10];
  unsigned int size;
} vector_coinID_t;

void hex2bin(uint8_t *out_bytes, const char *hex_string, const size_t size_string)
{
  uint32_t buffer = 0;
  for (size_t i = 0; i < size_string / 2; i++)
  {
    sscanf(hex_string + 2 * i, "%2X", &buffer);
    out_bytes[i] = buffer;
  }
}

void coin_id_set_subkey(BeamCrypto_CoinID *coin_id, uint32_t sub_idx)
{
  // Scheme::V1
  static const uint32_t nScheme = BeamCrypto_CoinID_Scheme_V1;
  static const uint32_t s_SubKeyBits = 24;
  static const uint32_t s_SubKeyMask = (1 << s_SubKeyBits) - 1;
  coin_id->m_SubIdx = (sub_idx & s_SubKeyMask) | (nScheme << s_SubKeyBits);
}

void coin_id_set_subkey_scheme(BeamCrypto_CoinID *coin_id, uint32_t sub_idx, uint32_t scheme)
{
  static const uint32_t s_SubKeyBits = 24;
  static const uint32_t s_SubKeyMask = (1 << s_SubKeyBits) - 1;
  coin_id->m_SubIdx = (sub_idx & s_SubKeyMask) | (scheme << s_SubKeyBits);
}

void coin_id_init(BeamCrypto_CoinID *coin_id)
{
  coin_id_set_subkey(coin_id, 0);
  coin_id->m_Type = 1852797549U; // Regular key type
  coin_id->m_Amount = 0;
  coin_id->m_AssetID = 0;
}

void bigint_init(BeamCrypto_UintBig *bigint)
{
  memset(bigint->m_pVal, 0, BeamCrypto_nBytes);
}

void compact_point_init(BeamCrypto_CompactPoint *compact_point)
{
  bigint_init(&compact_point->m_X);
  compact_point->m_Y = 0;
}

void tx_kernel_init(BeamCrypto_TxKernel *kernel)
{
  kernel->m_Fee = 0;
  kernel->m_hMin = 0;
  kernel->m_hMax = 0;

  compact_point_init(&kernel->m_Commitment);
  compact_point_init(&kernel->m_Signature.m_NoncePub);
  bigint_init(&kernel->m_Signature.m_k);
}

void tx_common_init(BeamCrypto_TxCommon *tx)
{
  tx->m_pIns = NULL;
  tx->m_pOuts = NULL;
  tx->m_Ins = 0;
  tx->m_Outs = 0;

  bigint_init(&tx->m_kOffset);
  tx_kernel_init(&tx->m_Krn);
}

void key_keeper_init(BeamCrypto_KeyKeeper *kk, uint8_t *seed)
{
  BeamCrypto_UintBig seed_hv;
  memcpy(seed_hv.m_pVal, seed, BeamCrypto_nBytes);
  BeamCrypto_Kdf_Init(&kk->m_MasterKey, &seed_hv);
}

void tx_common_set_io(BeamCrypto_TxCommon *tx, vector_coinID_t *inputs, vector_coinID_t *outputs)
{
  tx->m_pIns = inputs->coins;
  tx->m_pOuts = outputs->coins;
  tx->m_Ins = inputs->size;
  tx->m_Outs = outputs->size;
}

void vector_coinID_init(vector_coinID_t *vec)
{
  vec->size = 0;
}

BeamCrypto_CoinID *vector_coinID_add(vector_coinID_t *vec, BeamCrypto_Amount amount, uint64_t idx)
{
  BeamCrypto_CoinID *coin = &vec->coins[vec->size++];
  coin_id_init(coin);

  coin->m_Amount = amount;
  coin->m_Idx = idx;

  if (vec->size >= 10)
    printf("ERROR: out of bounds, increase vector size\n");

  return coin;
}

void set_well_known_32b(uint8_t *dest)
{
  hex2bin(dest, "751b77ab415ed14573b150b66d779d429e48cd2a40c51bf6ce651ce6c38fd620", 64);
}
