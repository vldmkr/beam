#pragma once
#include "wallet/common.h"
#include "core/proto.h"

namespace beam {

class WalletDirectIO {
public:
    using Ptr = std::unique_ptr<WalletDirectIO>;
    using Callback = std::function<WalletID*(proto::BbsMsg&& msg, bool& txEnded)>;

    /// creates an impl
    static Ptr create(io::Address listenTo, Callback&& callback);

    /// dtor
    virtual ~WalletDirectIO() {}

    /// associates direct connection address with peer or disables by passing empty address
    virtual void assign_direct_peer(const WalletID& walletAddress, io::Address networkAddress) = 0;

    /// tries to send msg via direct io, returns false if no direct address assigned to peer
    virtual bool try_send_direct(const WalletID& walletAddress, proto::BbsMsg&& msg) = 0;
};

} //namespace
