#include "receiver.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    Receiver::FSMDefinition::FSMDefinition(receiver::IGateway &gateway, beam::IKeyChain::Ptr keychain, sender::InvitationData::Ptr initData)
        : m_gateway{ gateway }
        , m_keychain{ keychain }
        , m_txId{ initData->m_txId }
        , m_amount{ initData->m_amount }
        , m_message{ initData->m_message }
        , m_publicSenderBlindingExcess{ initData->m_publicSenderBlindingExcess }
        , m_publicSenderNonce{ initData->m_publicSenderNonce }
        , m_transaction{ make_shared<Transaction>() }
    {
        m_transaction->m_vInputs = move(initData->m_inputs);
        m_transaction->m_vOutputs = move(initData->m_outputs);
    }

    void Receiver::FSMDefinition::confirm_tx(const msmf::none&)
    {
        auto confirmationData = make_shared<receiver::ConfirmationData>();
        confirmationData->m_txId = m_txId;

        TxKernel::Ptr kernel = make_unique<TxKernel>();
        kernel->m_Fee = 0;
        kernel->m_HeightMin = 0;
        kernel->m_HeightMax = static_cast<Height>(-1);
        m_kernel = kernel.get();
        m_transaction->m_vKernels.push_back(move(kernel));

        // 1. Check fee
        // 2. Create receiver_output
        // 3. Choose random blinding factor for receiver_output
        Amount amount = m_amount;
        Output::Ptr output = make_unique<Output>();
        output->m_Coinbase = false;

        Scalar::Native blindingFactor = m_keychain->getNextKey();

        Point::Native pt;
        pt = Commitment(blindingFactor, amount);
        output->m_Commitment = pt;

        output->m_pPublic.reset(new RangeProof::Public);
        output->m_pPublic->m_Value = amount;
        output->m_pPublic->Create(blindingFactor);

        m_blindingExcess = -blindingFactor; // TODO: we have to remove this negation and change signs in verification formula

        m_transaction->m_vOutputs.push_back(move(output));
 
        // 4. Calculate message M
        // 5. Choose random nonce
        Signature::MultiSig msig;
        m_nonce = generateNonce();
        msig.m_Nonce = m_nonce;
        // 6. Make public nonce and blinding factor
        m_publicReceiverBlindingExcess = Context::get().G * m_blindingExcess;
        confirmationData->m_publicReceiverBlindingExcess = m_publicReceiverBlindingExcess;

        Point::Native publicNonce;
        publicNonce = Context::get().G * m_nonce;
        confirmationData->m_publicReceiverNonce = publicNonce;
        // 7. Compute Shnorr challenge e = H(M|K)

        msig.m_NoncePub = m_publicSenderNonce + confirmationData->m_publicReceiverNonce;
        // 8. Compute recepient Shnorr signature
        m_kernel->m_Signature.CoSign(m_receiverSignature, m_message, m_blindingExcess, msig);
        
        confirmationData->m_receiverSignature = m_receiverSignature;

        m_gateway.send_tx_confirmation(confirmationData);
    }

    bool Receiver::FSMDefinition::is_valid_signature(const TxConfirmationCompleted& event)
    {
        auto data = event.data;
        // 1. Verify sender's Schnor signature
        Scalar::Native ne = m_kernel->m_Signature.m_e;
        ne = -ne;
        Point::Native s, s2;

        s = m_publicSenderNonce;
        s += m_publicSenderBlindingExcess * ne;

        s2 = Context::get().G * data->m_senderSignature;
        Point p(s), p2(s2);

        return (p == p2);
    }

    bool Receiver::FSMDefinition::is_invalid_signature(const TxConfirmationCompleted& event)
    {
        return !is_valid_signature(event);
    }

    void Receiver::FSMDefinition::register_tx(const TxConfirmationCompleted& event)
    {
        // 2. Calculate final signature
        Scalar::Native senderSignature;
        senderSignature = event.data->m_senderSignature;
        Scalar::Native finialSignature = senderSignature + m_receiverSignature;

        // 3. Calculate public key for excess
        Point::Native x = m_publicReceiverBlindingExcess;
        x += m_publicSenderBlindingExcess;
        // 4. Verify excess value in final transaction
        // 5. Create transaction kernel
        m_kernel->m_Excess = x;
        m_kernel->m_Signature.m_k = finialSignature;

        // 6. Create final transaction and send it to mempool
     
        //beam::TxBase::Context ctx;
        //assert(m_transaction->IsValid(ctx));

        auto data = shared_ptr<receiver::RegisterTxData>(new receiver::RegisterTxData{ m_txId, move(m_transaction) });
        m_gateway.register_tx(data);
    }

    void Receiver::FSMDefinition::rollback_tx(const TxFailed& event)
    {
        cout << "Receiver::rollback_tx\n";
    }

    void Receiver::FSMDefinition::cancel_tx(const TxConfirmationCompleted& )
    {
        cout << "Receiver::cancel_tx\n";
    }

    void Receiver::FSMDefinition::confirm_output(const TxRegistrationCompleted& )
    {
        m_gateway.send_tx_registered(make_unique<Uuid>(m_txId));
        m_gateway.send_output_confirmation();
    }

    void Receiver::FSMDefinition::complete_tx(const TxOutputConfirmCompleted& )
    {
        cout << "Receiver::complete_tx\n";
    }
}