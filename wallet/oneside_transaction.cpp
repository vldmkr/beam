// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "oneside_transaction.h"

using namespace std;

namespace beam { namespace wallet {

    BaseTransaction::Ptr OneSideTransaction::Create(INegotiatorGateway& gateway
        , beam::IWalletDB::Ptr walletDB
        , const TxID& txID)
    {
        //return make_shared<OneSideTransaction>(gateway, walletDB, txID);
        return BaseTransaction::Ptr(new OneSideTransaction(gateway, walletDB, txID));
       
    }

    OneSideTransaction::OneSideTransaction(INegotiatorGateway& gateway
        , beam::IWalletDB::Ptr walletDB
        , const TxID& txID)
        : BaseTransaction(gateway, walletDB, txID)
    {

    }

    TxType OneSideTransaction::GetType() const
    {
        return TxType::OneSide;
    }

    void OneSideTransaction::UpdateImpl()
    {
        bool isSender = GetMandatoryParameter<bool>(TxParameterID::IsSender);

        AmountList amountList;
        if (!GetParameter(TxParameterID::AmountList, amountList))
        {
            amountList = AmountList{ GetMandatoryParameter<Amount>(TxParameterID::Amount) };
        }

        if (!isSender)
        {

            wallet::TxBuilder receiverBuilder(*this, amountList, 0);

            if (receiverBuilder.GenerateBlindingExcess())
            {
                for (const auto& amount : receiverBuilder.GetAmountList())
                {
                    receiverBuilder.GenerateNewCoin(amount, false);
                }
                receiverBuilder.CreateOutputs();
                receiverBuilder.FinalizeOutputs();
                receiverBuilder.CreateKernel();
                receiverBuilder.SignPartial();
                receiverBuilder.FinalizeSignature();
                UpdateOnNextTip();
                return;
            }
            //m_Gateway.ConfirmOutputs(receiverBuilder.GetCoinIDs());
            return;
        }

        auto sharedBuilder = make_shared<TxBuilder>(*this, amountList, GetMandatoryParameter<Amount>(TxParameterID::Fee));
        TxBuilder& builder = *sharedBuilder;

        builder.GenerateBlindingExcess();

        if (!builder.GetPeerKernels())
        {
            OnFailed(TxFailureReason::MaxHeightIsUnacceptable, true);
        }

        if (!builder.GetInitialTxParams())
        {
            LOG_INFO() << GetTxID() << (isSender ? " Sending " : " Receiving ")
                << PrintableAmount(builder.GetAmount())
                << " (fee: " << PrintableAmount(builder.GetFee()) << ")";

            if (isSender)
            {
                Height maxResponseHeight = 0;
                if (GetParameter(TxParameterID::PeerResponseHeight, maxResponseHeight))
                {
                    LOG_INFO() << GetTxID() << " Max height for response: " << maxResponseHeight;
                }

                builder.SelectInputs();
                builder.AddChange();
            }

            if (!isSender)
            {
                // create receiver utxo
                for (const auto& amount : builder.GetAmountList())
                {
                    builder.GenerateNewCoin(amount, false);
                }
            }

            UpdateTxDescription(TxStatus::InProgress);

            if (!builder.GetCoins().empty())
            {
                sharedBuilder->CreateOutputs();
                sharedBuilder->FinalizeOutputs();
            }
        }

        uint64_t nAddrOwnID;
        if (!GetParameter(TxParameterID::MyAddressID, nAddrOwnID))
        {
            WalletID wid;
            if (GetParameter(TxParameterID::MyID, wid))
            {
                auto waddr = m_WalletDB->getAddress(wid);
                if (waddr && waddr->m_OwnID)
                    SetParameter(TxParameterID::MyAddressID, waddr->m_OwnID);
            }
        }

        builder.GenerateNonce();

        if (!builder.UpdateMaxHeight())
        {
            OnFailed(TxFailureReason::MaxHeightIsUnacceptable, true);
            return;
        }

        builder.CreateKernel();
        builder.SignPartial();

        if (!builder.GetPeerInputsAndOutputs())
        {
            OnFailed(TxFailureReason::MaxHeightIsUnacceptable, true);
            return;
        }

        if (IsInitiator() && !builder.IsPeerSignatureValid())
        {
            OnFailed(TxFailureReason::InvalidPeerSignature, true);
            return;
        }

        builder.FinalizeSignature();

        bool isRegistered = false;
        if (!GetParameter(TxParameterID::TransactionRegistered, isRegistered))
        {
            if (CheckExpired())
            {
                return;
            }

            // Construct transaction
            auto transaction = builder.CreateTransaction();

            // Verify final transaction
            TxBase::Context::Params pars;
            TxBase::Context ctx(pars);
            if (!transaction->IsValid(ctx))
            {
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return;
            }
            m_Gateway.register_tx(GetTxID(), transaction);
//            SetState(State::Registration);
            return;
        }

        if (!isRegistered)
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return;
        }

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            ConfirmKernel(builder.GetKernel());
            return;
        }

        vector<Coin> modified = m_WalletDB->getCoinsByTx(GetTxID());
        for (auto& coin : modified)
        {
            bool bIn = (coin.m_createTxId == m_ID);
            bool bOut = (coin.m_spentTxId == m_ID);
            if (bIn || bOut)
            {
                if (bIn)
                {
                    coin.m_confirmHeight = std::min(coin.m_confirmHeight, hProof);
                    coin.m_maturity = hProof + Rules::get().Maturity.Std; // so far we don't use incubation for our created outputs
                }
                if (bOut)
                    coin.m_spentHeight = std::min(coin.m_spentHeight, hProof);
            }
        }

        GetWalletDB()->save(modified);

        CompleteTx();
    }
}
}