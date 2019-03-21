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

    }
}
}