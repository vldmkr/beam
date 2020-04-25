#ifndef __BEAM_PURE_TEST_FUNCTIONS__
#define __BEAM_PURE_TEST_FUNCTIONS__

#include "local_test_utils.h"

int invoke_SignSplit(const BeamCrypto_KeyKeeper *pCtx, BeamCrypto_TxCommon *pTx)
{
  int ret = BeamCrypto_KeyKeeper_SignTx_Split(pCtx, pTx);
  // printf("BeamCrypto_KeyKeeper_SignTx_Split %d\n", ret);
  return ret;
}

void test_manual_SignSplit(void)
{
  uint8_t seed[BeamCrypto_nBytes];
  set_well_known_32b(seed);

  BeamCrypto_KeyKeeper ctx;
  key_keeper_init(&ctx, seed);
  ctx.m_AllowWeakInputs = 0;

  BeamCrypto_TxCommon tx_SignSplit;
  tx_common_init(&tx_SignSplit);

  vector_coinID_t inputs;
  vector_coinID_t outputs;

  vector_coinID_init(&inputs);
  vector_coinID_init(&outputs);

  vector_coinID_add(&inputs, 55, 101);
  vector_coinID_add(&inputs, 16, 102);

  vector_coinID_add(&outputs, 12, 103);
  vector_coinID_add(&outputs, 13, 104);
  vector_coinID_add(&outputs, 14, 105);

  tx_common_set_io(&tx_SignSplit, &inputs, &outputs);

  const int g_hFork = 3;
  tx_SignSplit.m_Krn.m_hMin = g_hFork;
  tx_SignSplit.m_Krn.m_hMax = g_hFork + 40;
  tx_SignSplit.m_Krn.m_Fee = 30; // Incorrect balance (funds missing)

  VERIFY_TEST(invoke_SignSplit(&ctx, &tx_SignSplit) != BeamCrypto_KeyKeeper_Status_Ok);

  tx_SignSplit.m_Krn.m_Fee = 32; // ok

  coin_id_set_subkey_scheme(&outputs.coins[0], 0, BeamCrypto_CoinID_Scheme_V0); // weak output scheme
  VERIFY_TEST(invoke_SignSplit(&ctx, &tx_SignSplit) != BeamCrypto_KeyKeeper_Status_Ok);

  coin_id_set_subkey_scheme(&outputs.coins[0], 0, BeamCrypto_CoinID_Scheme_BB21); // weak output scheme
  VERIFY_TEST(invoke_SignSplit(&ctx, &tx_SignSplit) != BeamCrypto_KeyKeeper_Status_Ok);

  coin_id_set_subkey(&outputs.coins[0], 12); // outputs to a child key
  VERIFY_TEST(invoke_SignSplit(&ctx, &tx_SignSplit) != BeamCrypto_KeyKeeper_Status_Ok);

  coin_id_set_subkey(&outputs.coins[0], 0); // ok

  coin_id_set_subkey_scheme(&inputs.coins[0], 14, BeamCrypto_CoinID_Scheme_V0); // weak input scheme
  VERIFY_TEST(invoke_SignSplit(&ctx, &tx_SignSplit) != BeamCrypto_KeyKeeper_Status_Ok);

  ctx.m_AllowWeakInputs = 1;
  VERIFY_TEST(invoke_SignSplit(&ctx, &tx_SignSplit) == BeamCrypto_KeyKeeper_Status_Ok); // should work now
  DEBUG_PRINT("Kernel.Signature.K.Val", tx_SignSplit.m_Krn.m_Signature.m_k.m_pVal, BeamCrypto_nBytes);
  VERIFY_TEST(IS_EQUAL_HEX(
      "e47013d204037f5e441eb893d3791cbd297a3e0e0ff2641d825a3a3bbbdec9fc",
      tx_SignSplit.m_Krn.m_Signature.m_k.m_pVal, BeamCrypto_nBytes * 2));

  // add asset
  vector_coinID_add(&inputs, 16, 106)->m_AssetID = 12;
  vector_coinID_add(&outputs, 16, 107)->m_AssetID = 13;
  tx_common_set_io(&tx_SignSplit, &inputs, &outputs);

  VERIFY_TEST(invoke_SignSplit(&ctx, &tx_SignSplit) != BeamCrypto_KeyKeeper_Status_Ok); // different assets mixed (not allowed)

  outputs.coins[outputs.size - 1].m_AssetID = 12;
  outputs.coins[outputs.size - 1].m_Amount = 15;
  VERIFY_TEST(invoke_SignSplit(&ctx, &tx_SignSplit) != BeamCrypto_KeyKeeper_Status_Ok); // asset balance mismatch

  outputs.coins[outputs.size - 1].m_Amount = 16;
  memset(tx_SignSplit.m_kOffset.m_pVal, 0, BeamCrypto_nBytes);
  VERIFY_TEST(invoke_SignSplit(&ctx, &tx_SignSplit) == BeamCrypto_KeyKeeper_Status_Ok); // ok
}

#endif // __BEAM_PURE_TEST_FUNCTIONS__
