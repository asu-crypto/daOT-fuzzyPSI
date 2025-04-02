#include "./SpL2.h"
#include "../Common/ZN.h"
#include "../SpBSOT/SpBSOT.h"
#include "../BlockSpBSOT/BlockSpBSOT.h"
#include "../CustomOPRF/CustomizedOPRF.h"
#include "coproto/Socket/Socket.h"
#include <array>
#include <cmath>

#define MAX_SSP 128
#define IN_COMP_BIT_LEN 8

using std::floor;
using std::abs;

template<typename T, size_t N>
using array = std::array<T, N>;

template<size_t tr, size_t ts, size_t k, size_t n>
using BlockSpBSOTSender = sparse_comp::block_sp_bsot::Sender<tr,ts,k,n>; 

template<size_t ts, size_t tr, size_t k, size_t n>
using BlockSpBSOTReceiver = sparse_comp::block_sp_bsot::Receiver<ts,tr,k,n>; 

template<size_t tr, size_t t, size_t k, size_t n, uint64_t M>
using SpBSOTSender = sparse_comp::sp_bsot::Sender<tr,t,k,n,M>;

template<size_t ts, size_t t, size_t k, size_t n, uint64_t M>
using SpBSOTReceiver = sparse_comp::sp_bsot::Receiver<ts,t,k,n,M>;

using OprfSender = sparse_comp::custom_oprf::Sender;
using OprfReceiver = sparse_comp::custom_oprf::Receiver;

using Proto = coproto::task<void>;

template<size_t t>
void hash_z_shares(AES& aes, array<array<block,1>,t>& z_vec_shares, array<block,t>& hashed_z_shares) {

    for (size_t i=0;i < t;i++) {
        
        hashed_z_shares[i] = aes.hashBlock(z_vec_shares[i][0]);

    }

}

template<size_t t, uint64_t N>
static void gen_zero_shares(array<array<ZN<N>,2>,t>& zero_shares) {

    for (size_t i=0;i < t;i++) {
        for (size_t j=0;j < 2;j++) {
            zero_shares[i][j] = ZN<N>(0);
        }
    }

}

template<size_t tr, size_t ts, uint16_t twotol, uint8_t delta, uint64_t M>
static Proto sender_compute_h_shares(OprfSender* oprfSender,
                                    OprfReceiver* oprfReceiver,
                                    Socket& sock, 
                                    PRNG& prng,
                                    array<point,ts>& ordIndexSet,
                                    array<array<ZN<twotol>,2>,ts>& in_vals,
                                    array<array<ZN<M>,2>,ts>& h_shares) {    
    static_assert(M == 2*(delta + 1) + 1,"the following identity must be fulfilled: M = 2*(delta + 1) + 1");

    constexpr const int32_t delta_sq = delta*delta;

    MC_BEGIN(Proto, oprfSender, oprfReceiver, &sock, &prng, &ordIndexSet, &in_vals, &h_shares,
             zero_shares = (array<array<ZN<twotol>,2>,ts>*) nullptr,
             msg_vecs = (array<array<array<ZN<M>,twotol>,2>,ts>*) nullptr,
             bsotSender = (SpBSOTSender<tr,ts,2,twotol,M>*) nullptr,
             diff = int32_t(0),
             abs_diff = int32_t(0));
        zero_shares = new array<array<ZN<twotol>,2>,ts>();
        msg_vecs = new array<array<array<ZN<M>,twotol>,2>,ts>();
        bsotSender = new SpBSOTSender<tr,ts,2,twotol,M>(prng, oprfSender, oprfReceiver);

        for (size_t i=0;i < ts;i++) {
            array<ZN<M>,twotol>& msg_vec = msg_vecs->at(i)[0];

            for (int32_t h=0;h < twotol;h++) {

                abs_diff = (int32_t) abs(h-(int32_t) in_vals[i][0].to_uint64_t());
                msg_vec[h] = (abs_diff <= delta) ? ZN<M>(abs_diff) : ZN<M>(delta+1);
                
            }
        }

        for (size_t i=0;i < ts;i++) {
            array<ZN<M>,twotol>& msg_vec = msg_vecs->at(i)[1];

            for (int32_t h=0;h < twotol;h++) {

               int32_t x_sq_lb = delta_sq - (int32_t) (h - (int32_t) in_vals[i][1].to_uint64_t())*(h - (int32_t) in_vals[i][1].to_uint64_t());
               int32_t min_x = x_sq_lb <= 0 ? 0 : floor(sqrt((double) x_sq_lb) + 1.0);

                msg_vec[h] = ZN<M>((delta+1) - min_x);
               
            }
        
        }

        gen_zero_shares<ts,twotol>(*zero_shares);

        MC_AWAIT(bsotSender->send(sock, ordIndexSet, *msg_vecs, *zero_shares, h_shares));

        delete zero_shares;
        delete msg_vecs;
        delete bsotSender;

    MC_END();

}


template<size_t ts, size_t tr, uint16_t twotol, uint8_t delta, uint64_t M>
static Proto recvr_compute_h_shares(OprfReceiver* oprfReceiver,
                                   OprfSender* oprfSender,
                                   Socket& sock, 
                                   PRNG& prng,
                                   array<point,tr>& ordIndexSet,
                                   array<array<ZN<twotol>,2>,tr>& in_vals,
                                   array<array<ZN<M>,2>,tr>& h_shares) {
    
    static_assert(M == 2*(delta + 1) + 1,"the following identity must be fulfilled: M = 2*(delta + 1) + 1");
    
    MC_BEGIN(Proto, oprfReceiver, oprfSender, &sock, &prng, &ordIndexSet, &in_vals, &h_shares,
             bsotReceiver = (SpBSOTReceiver<ts, tr,2,twotol,M>*) nullptr);
        
        bsotReceiver = new SpBSOTReceiver<ts, tr,2,twotol,M>(prng, oprfReceiver, oprfSender);
       
        MC_AWAIT(bsotReceiver->receive(sock, ordIndexSet, in_vals, h_shares));

        delete bsotReceiver;

    MC_END();

}

template<size_t t, uint64_t N>
static void in_values_to_zn(array<array<uint32_t,2>,t>& in_values, array<array<ZN<N>,2>,t>& convted_values) {

    for (size_t i=0;i < t;i++) {
        for (size_t j=0;j < 2;j++) {
            convted_values[i][j] = ZN<N>(in_values[i][j]);
        }
    }

}

template<size_t t, uint8_t delta, uint64_t M>
static void comp_g_shares(array<array<ZN<M>,2>,t>& h_vec_shares,
                          array<array<ZN<M>,1>,t>& g_shares) {

    static_assert(M == 2*(delta + 1) + 1,"the following identity must be fulfilled: M = 2*(delta + 1) + 1");

    for (size_t i=0;i < t;i++) {
        g_shares[i][0] = ZN<M>(0);

        for (size_t j=0;j < 2;j++) {
            
            g_shares[i][0] = g_shares[i][0] + h_vec_shares[i][j];

        }

    }

}

template<size_t tr, size_t ts, uint8_t delta, uint64_t M>
static Proto sender_comp_z_shares(OprfSender* oprfSender,
                                  OprfReceiver* oprfReceiver,
                                  Socket& sock, 
                                  PRNG& prng,
                                  array<point,ts>& ordIndexSet,
                                  array<array<ZN<M>,1>,ts>& g_vec_shares,
                                  array<array<block,1>,ts>& z_vec_shares) {
    static_assert(M == 2*(delta + 1) + 1,"the following identity must be fulfilled: M = 2*(delta + 1) + 1");

    MC_BEGIN(Proto, oprfSender, oprfReceiver, &sock, &prng, &ordIndexSet, &g_vec_shares, &z_vec_shares,
             msg_vecs = (array<array<array<block,M>,1>,ts>*) nullptr,
             bsotSender = (BlockSpBSOTSender<tr,ts, 1, M>*) nullptr,
             zb = block(0,0),
             r = block(0,0));
        msg_vecs = new array<array<array<block,M>,1>,ts>();
        bsotSender = new BlockSpBSOTSender<tr,ts, 1, M>(prng, oprfSender, oprfReceiver);

        for (size_t i=0;i < ts;i++) {
            r = prng.get<block>();
            for (size_t j=0;j < M;j++) {
                if(j <= delta) {
                    msg_vecs->at(i)[0][j] = block(0,0);
                } else {
                    msg_vecs->at(i)[0][j] = r;
                }
            }
        }

        MC_AWAIT(bsotSender->send(sock, ordIndexSet, *msg_vecs, g_vec_shares, z_vec_shares));

        delete msg_vecs;
        delete bsotSender;

    MC_END();
}

template<size_t ts, size_t tr, uint8_t delta, uint64_t M>
static Proto receiver_comp_z_shares(OprfReceiver* oprfReceiver,
                                    OprfSender* oprfSender,
                                    Socket& sock, 
                                    PRNG& prng,
                                    array<point,tr>& ordIndexSet,
                                    array<array<ZN<M>,1>,tr>& g_vec_shares,
                                    array<array<block,1>,tr>& z_vec_shares) {
    static_assert(M == 2*(delta + 1) + 1,"the following identity must be fulfilled: M = 2*(delta + 1) + 1");

    MC_BEGIN(Proto, oprfReceiver, oprfSender, &sock, &prng, &ordIndexSet, &g_vec_shares, &z_vec_shares,
             bsotReceiver = (BlockSpBSOTReceiver<ts,tr, 1, M>*) nullptr);

        bsotReceiver = new BlockSpBSOTReceiver<ts,tr, 1, M>(prng, oprfReceiver, oprfSender);

        MC_AWAIT(bsotReceiver->receive(sock, ordIndexSet, g_vec_shares, z_vec_shares));

        delete bsotReceiver;

    MC_END();
}

template<size_t t>
void hash_z_shares(AES& aes, array<array<block,1>,t>& z_vec_shares, vector<block>& hashed_z_shares) {

    for (size_t i=0;i < t;i++) {
        
        hashed_z_shares[i] = aes.hashBlock(z_vec_shares[i][0]);

    }

}

template<size_t ts, size_t tr>
static void compute_intersec_hashed_z_shares(AES& aes, 
                                             array<array<block,1>,tr>& rec_z_shares, 
                                             vector<block>& hashed_sen_z_shares,
                                             vector<size_t>& inter_pos) {

    vector<block>* hashed_rec_z_shares = new vector<block>(tr);
    
    hash_z_shares(aes, rec_z_shares, *hashed_rec_z_shares);

    std::unordered_set<block> set;

    for (size_t i=0;i < ts;i++) {
        set.insert(hashed_sen_z_shares[i]);
    }

    for(size_t i=0;i < tr;i++) {
        if(set.contains(hashed_rec_z_shares->at(i))) {
            inter_pos.push_back(i);
        }
    }

    delete hashed_rec_z_shares;

}

template<size_t tr, size_t ts, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_l2::Sender<tr,ts,delta,ssp>::send(Socket& sock, 
                                                          array<point,ts>& ordIndexSet,
                                                          array<array<uint32_t,2>,ts>& in_values) {
    static_assert(ssp <= MAX_SSP,"ssp must be less or equal to 128");
    
    constexpr uint8_t l = IN_COMP_BIT_LEN;
    static_assert(l <= 8,"l must be less or equal to 8");
    constexpr const uint16_t twotol = (uint16_t) pow(2, l);
    constexpr const uint64_t M = 2*(delta + 1) + 1;

    //constexpr const uint64_t two_to_ssp = std::pow(2,ssp);

    constexpr const size_t oprf_instances = 2;

    MC_BEGIN(Proto, this, &sock, &ordIndexSet, &in_values,
             zn_in_values = (array<array<ZN<twotol>,2>,ts>*) nullptr,
             h_vec_shares = (array<array<ZN<M>,2>,ts>*) nullptr,
             g_vec_shares = (array<array<ZN<M>,1>,ts>*) nullptr,
             z_vec_shares = (array<array<block,1>,ts>*) nullptr,
             hashed_z_shares = vector<block>(),
             oprfSenders = std::vector<OprfSender*>(oprf_instances),
             oprfReceivers = std::vector<OprfReceiver*>(oprf_instances),
             prt = Proto());

        MC_AWAIT(OprfSender::setup(sock, *(this->prng), oprf_instances, oprfSenders)); // Setup OPRFs
        MC_AWAIT(OprfReceiver::setup(sock, *(this->prng), oprf_instances, oprfReceivers)); // Setup OPRFs

        zn_in_values = new array<array<ZN<twotol>,2>,ts>();
        in_values_to_zn<ts,twotol>(in_values, *zn_in_values);

        h_vec_shares = new array<array<ZN<M>,2>,ts>();
        prt = sender_compute_h_shares<tr,ts,twotol,delta,M>(oprfSenders[0], oprfReceivers[0], sock, *(this->prng), ordIndexSet, *zn_in_values, *h_vec_shares);
        MC_AWAIT(prt);
        delete zn_in_values;

        //std::cout << "(s) h_vec_shares[0][0]: " << h_vec_shares->at(0)[0].to_uint64_t() << " h_vec_shares[0][1]: " << h_vec_shares->at(0)[1].to_uint64_t() << std::endl;
        //std::cout << "(s) h_vec_shares[1][0]: " << h_vec_shares->at(1)[0].to_uint64_t() << " h_vec_shares[1][1]: " << h_vec_shares->at(1)[1].to_uint64_t() << std::endl;

        g_vec_shares = new array<array<ZN<M>,1>,ts>();
        comp_g_shares<ts,delta,M>(*h_vec_shares, *g_vec_shares);
        delete h_vec_shares;

        //std::cout << "(s) g_vec_shares[0][0]: " << g_vec_shares->at(0)[0].to_uint64_t() << std::endl;
        //std::cout << "(s) g_vec_shares[1][0]: " << g_vec_shares->at(1)[0].to_uint64_t() << std::endl;

        z_vec_shares = new array<array<block,1>,ts>();
        prt = sender_comp_z_shares<tr,ts,delta,M>(oprfSenders[1], oprfReceivers[1], sock, *(this->prng), ordIndexSet, *g_vec_shares, *z_vec_shares);
        MC_AWAIT(prt);
        delete g_vec_shares;

        //std::cout << "(s) z_vec_shares[0][0]: " << z_vec_shares->at(0)[0] << std::endl;
        //std::cout << "(s) z_vec_shares[1][0]: " << z_vec_shares->at(1)[0] << std::endl;

        hashed_z_shares.resize(ts);
        hash_z_shares<ts>(*(this->aes), *z_vec_shares, hashed_z_shares);
        delete z_vec_shares;

        //std::cout << "(s) hashed_z_shares[0]: " << hashed_z_shares[0] << std::endl;
        //std::cout << "(s) hashed_z_shares[1]: " << hashed_z_shares[1] << std::endl;

        prt = sparse_comp::send<block,sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(sock, hashed_z_shares);
        MC_AWAIT(prt);

    MC_END();

}

template<size_t ts, size_t tr, uint8_t delta, uint8_t ssp>
Proto sparse_comp::sp_l2::Receiver<ts,tr,delta,ssp>::receive(Socket& sock, 
                                                                            array<point,tr>& ordIndexSet, 
                                                                            array<array<uint32_t,2>,tr>& in_values, 
                                                                            std::vector<size_t>& intersec_pos) {
    static_assert(ssp <= MAX_SSP,"ssp must be less or equal to 128");

    constexpr const uint8_t l = IN_COMP_BIT_LEN;
    static_assert(l <= 8,"l must be less or equal to 8");
    constexpr const uint16_t twotol = (uint16_t) pow(2, l);
    constexpr const uint64_t M = 2*(delta + 1) + 1;

    //constexpr const uint64_t two_to_ssp = std::pow(2,ssp);

    constexpr const size_t oprf_instances = 2;

    MC_BEGIN(Proto, this, &sock, &ordIndexSet, &in_values, &intersec_pos,
             zn_in_values = (array<array<ZN<twotol>,2>,tr>*) nullptr,
             h_vec_shares = (array<array<ZN<M>,2>,tr>*) nullptr,
             g_vec_shares = (array<array<ZN<M>,1>,tr>*) nullptr,
             z_vec_shares =  (array<array<block,1>,tr>*) nullptr,
             sender_hashed_z_sender_shares = vector<block>(),
             oprfReceivers = std::vector<OprfReceiver*>(oprf_instances),
             oprfSenders = std::vector<OprfSender*>(oprf_instances),
             prt = Proto());

        MC_AWAIT(OprfReceiver::setup(sock, *(this->prng), oprf_instances, oprfReceivers)); // Setup OPRFs
        MC_AWAIT(OprfSender::setup(sock, *(this->prng), oprf_instances, oprfSenders)); // Setup OPRFs
        
        zn_in_values = new array<array<ZN<twotol>,2>,tr>();
        in_values_to_zn<tr,twotol>(in_values, *zn_in_values);

        h_vec_shares = new array<array<ZN<M>,2>,tr>();
        prt = recvr_compute_h_shares<ts,tr,twotol,delta,M>(oprfReceivers[0], oprfSenders[0], sock, *(this->prng), ordIndexSet, *zn_in_values, *h_vec_shares);
        MC_AWAIT(prt);
        delete zn_in_values;

        //std::cout << "(r) h_vec_shares[0][0]: " << h_vec_shares->at(0)[0].to_uint64_t() << " h_vec_shares[0][1]: " << h_vec_shares->at(0)[1].to_uint64_t() << std::endl;
        //std::cout << "(r) h_vec_shares[1][0]: " << h_vec_shares->at(1)[0].to_uint64_t() << " h_vec_shares[1][1]: " << h_vec_shares->at(1)[1].to_uint64_t() << std::endl;

        g_vec_shares = new array<array<ZN<M>,1>,tr>();
        comp_g_shares<tr,delta,M>(*h_vec_shares, *g_vec_shares);
        delete h_vec_shares;

        //std::cout << "(r) g_vec_shares[0][0]: " << g_vec_shares->at(0)[0].to_uint64_t() << std::endl;
        //std::cout << "(r) g_vec_shares[1][0]: " << g_vec_shares->at(1)[0].to_uint64_t() << std::endl;

        z_vec_shares = new array<array<block,1>,tr>();
        prt = receiver_comp_z_shares<ts,tr,delta,M>(oprfReceivers[1], oprfSenders[1], sock, *(this->prng), ordIndexSet, *g_vec_shares, *z_vec_shares);
        MC_AWAIT(prt);
        delete g_vec_shares;

        //std::cout << "(r) z_vec_shares[0][0]: " << z_vec_shares->at(0)[0] << std::endl;
        //std::cout << "(r) z_vec_shares[1][0]: " << z_vec_shares->at(1)[0] << std::endl;

        sender_hashed_z_sender_shares.resize(ts);
        prt = sparse_comp::receive<block,sparse_comp::COPROTO_MAX_SEND_SIZE_BYTES>(sock, ts, sender_hashed_z_sender_shares);
        MC_AWAIT(prt);

        compute_intersec_hashed_z_shares<ts,tr>(*(this->aes), *z_vec_shares, sender_hashed_z_sender_shares, intersec_pos);

        /*for (size_t i = 0; i < intersec_pos.size(); ++i) {
            std::cout << "(r) intersec_pos[" << i << "]: " << intersec_pos[i] << std::endl;
        }*/

        delete z_vec_shares;
    
    MC_END();

}