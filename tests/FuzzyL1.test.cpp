#include "catch2/catch_test_macros.hpp"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/block.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "cryptoTools/Crypto/AES.h"
#include "../sparseComp/Common/Common.h"
#include "../sparseComp/Common/HashUtils.h"
#include "../sparseComp/FuzzyL1/FuzzyL1.h"
#include <cstdint>
#include <array>
#include <vector>
#include <set>
#include <unordered_map>
#include <utility>
#include <cmath>

static const int64_t MAX_U8_BIT_VAL = 255;
static const int64_t MAX_U32_BIT_VAL = 4294967295;

using sparse_comp::point;

using coproto::LocalAsyncSocket;

using PRNG = osuCrypto::PRNG;
using osuCrypto::block;
using AES = osuCrypto::AES;

using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;
using sparse_comp::hash_point;

using std::array;
using std::set;
using std::unordered_map;

using macoro::sync_wait;
using macoro::when_all_ready;

template <typename T, size_t n>
static void shuffle_array(PRNG & prng, array<T, n>& arr) {

    for (size_t i = 0; i < n; i++) {
        size_t j = prng.get<size_t>() % n;
        std::swap(arr[i], arr[j]);
    }
}

template <typename T>
static void shuffle_vector(PRNG & prng, vector<T> & vec) {

    for (size_t i = 0; i < vec.size(); i++) {
        size_t j = prng.get<size_t>() % vec.size();
        std::swap(vec[i], vec[j]);
    }

}

template <typename T1, typename T2, size_t n>
static void shuffle_array_pair(PRNG & prng, array<T1, n>& arr1, array<T2, n>& arr2) {

    for (size_t i = 0; i < n; i++) {
        size_t j = prng.get<size_t>() % n;
        std::swap(arr1[i], arr1[j]);
        std::swap(arr2[i], arr2[j]);
    }

}

// Picks n random non repeating element in the range [start,end].
static void pick_rand_from_seq(PRNG &prng, 
                                size_t start, 
                                size_t end, 
                                size_t n, 
                                vector<size_t> &out) {
    assert(start <= end);
    assert(n <= (end - start + 1));

    std::vector<size_t> seq;

    for (size_t i = start; i <= end; i++) {
        seq.push_back(i);
    }
    
    shuffle_vector(prng, seq);
    
    out.resize(n);

    for (size_t i = 0; i < n; i++) out[i] = seq[i];

}

/* Picks n random non repeating receiver points and values and puts them 
   in the beginning of the respective sender output arrays*/
template <size_t tr, size_t ts, size_t d, uint8_t delta>
static void pick_rand_rcvr_pts(AES& aes, PRNG& prng,
                                        array<point, tr> & rcvr_sparse_points,
                                        array<point, ts> & sndr_sparse_points,
                                        size_t n) {
    vector<size_t> rand_idxs(n);
    pick_rand_from_seq(prng, 0, tr - 1, n, rand_idxs);

    for (size_t i = 0; i < n; i++) {
        sndr_sparse_points[i] = rcvr_sparse_points[rand_idxs[i]];
    }

}

template<size_t d>
static point gen_rand_rcvr_point(PRNG& prng, uint32_t delta) {
    assert(d <= point::MAX_DIM);

    uint32_t lb = 2*delta;
    uint32_t ub = MAX_U32_BIT_VAL - 2*delta;

    uint32_t c[d];
    for (size_t i = 0; i < d; i++) {
        c[i] = (prng.get<uint32_t>() % (ub - lb + 1)) + lb;
    }

    return point(d, c);
}

template <size_t tr, size_t d>
static void samp_rcvr_pts(AES& aes, PRNG & prng, array<point, tr> & rcvr_points, uint8_t delta) {

    assert(d <= point::MAX_DIM);

    for (size_t i = 0; i < tr; i++) {
        rcvr_points[i] = gen_rand_rcvr_point<d>(prng, delta);
    }

}

template <size_t d, uint8_t delta>
static void samp_rand_linf_matching_ball(PRNG& prng,
                                         point& pt) {
    size_t sizet_delta = (size_t) delta;
    array<int64_t,d> offsets;
    std::fill(std::begin(offsets), std::end(offsets), 0);

    size_t total_offset = prng.get<size_t>() % (sizet_delta + 1);

    for (size_t i=0;i < total_offset;i++) {
        size_t j = prng.get<size_t>() % d;
        offsets[j]++;
    }

    for (size_t i = 0; i < d; i++) {
        bool orient = prng.get<bool>();
        pt[i] = orient ? pt[i] + offsets[i] : pt[i] - offsets[i];
    }

}

template <size_t d, uint8_t delta>
static array<uint32_t,d> smpl_sndr_rand_val(PRNG& prng) {
    array<uint32_t,d> pt;
    
    for (size_t i = 0; i < d; i++) {
        pt[i] = prng.get<uint8_t>();
    }

    return pt;
}

template<size_t d>
static point gen_rand_sndr_point(PRNG& prng) {
    assert(d <= point::MAX_DIM);

    uint32_t c[d];
    for (size_t i = 0; i < d; i++) {
        c[i] = prng.get<uint32_t>();
    }

    return point(d, c);
}

template <size_t tr, size_t ts, size_t d, uint8_t delta>
static void smpl_sndr_rand_pts(AES& aes, 
                               PRNG& prng,
                               size_t target_num_matching_pts,
                               array<point, tr> & rcvr_sparse_points,
                               array<point, ts>& sndr_sparse_points) {
    // Copies min_num_matching_random bins from rcvr_sparse_points to beginning of sndr_sparse_points.
    pick_rand_rcvr_pts<tr, ts, d, delta>(aes, prng, rcvr_sparse_points, sndr_sparse_points, target_num_matching_pts);

    // Randomizes min_num_matching_pts first points in sndr_in_values such that they are within delta of the corresponding points in rcvr_in_values.
    for (size_t i = 0; i < target_num_matching_pts; i++) {
        samp_rand_linf_matching_ball<d,delta>(prng, sndr_sparse_points[i]);
    }

    // Samples the rest of the points in sndr_points such that they are not within delta of the corresponding points in rcvr_points.
    for (size_t i = target_num_matching_pts; i < ts; i++) {
        sndr_sparse_points[i] = gen_rand_sndr_point<d>(prng);
    }

    shuffle_array<point,ts>(prng, sndr_sparse_points);

}

template <size_t tr, size_t ts, size_t d, uint8_t delta>
static void gen_constrained_rand_inputs(block seed,
                                        size_t target_num_matching_pts,
                                        array<point, tr> & rcvr_points,
                                        array<point, ts> & sndr_points) {
    assert(target_num_matching_pts <= ts && target_num_matching_pts <= tr);

    PRNG prng(seed);
    AES aes(prng.get<block>());

    samp_rcvr_pts<tr,d>(aes, prng, rcvr_points, delta);

    smpl_sndr_rand_pts<tr, ts, d, delta>(aes,
                                         prng, 
                                         target_num_matching_pts, 
                                         rcvr_points, 
                                         sndr_points);
}

template<size_t d>
static bool is_l1_close(point& pt,
                        point& ball_center, 
                        uint64_t delta) {
    int64_t acc_dist = 0;
    int64_t i64_delta = (int64_t) delta;

    for (size_t i = 0; i < d; i++) {

        int64_t i64_pt_i = pt[i];
        int64_t i64_ball_center_i = ball_center[i];

        int64_t dist = std::abs(i64_pt_i - i64_ball_center_i);

        if (dist > i64_delta) {
            return false;
        }

        acc_dist += dist;
    }

    return acc_dist <= i64_delta;

}

template<size_t tr, size_t ts, size_t d, uint8_t delta>
static void expected_l1_intersect(AES& aes,
                                    array<point, tr> & rcvr_points,
                                    array<point, ts> & sndr_points,
                                    vector<point>& intersec) {
    constexpr const size_t twotod = ((size_t) pow(2, d));
    constexpr const size_t recvr_cell_count = ((size_t) pow(2, d)) * tr;
                                        
    unordered_map<block,size_t> rcvr_pts_hashes;

    std::vector<block> rcvr_stcell_hashes;
    std::vector<block> sndr_st_hashes;

    sparse_comp::spatial_cell_hash<tr, d, recvr_cell_count>(aes, rcvr_points, rcvr_stcell_hashes, delta);
    sparse_comp::spatial_hash<ts>(aes, sndr_points, sndr_st_hashes, d, delta);

    for (size_t i=0;i < tr;i++) {
        for (size_t j=0;j < twotod;j++) {
            rcvr_pts_hashes.insert(std::make_pair(rcvr_stcell_hashes[twotod*i+j],i));
        }
    }
                                    
    for (size_t i = 0; i < ts; i++) {
        block hash = sndr_st_hashes[i];
        if (rcvr_pts_hashes.contains(hash)) {
            size_t r_idx = rcvr_pts_hashes[hash];

            if (is_l1_close<d>(rcvr_points[r_idx], sndr_points[i], delta)) {
                intersec.push_back(sndr_points[i]);
            }
        }
    }

}

template<size_t tr, size_t ts, size_t d, uint8_t delta>
static void safer_expected_l1_intersect(AES& aes,
                                    array<point, tr> & rcvr_points,
                                    array<point, ts> & sndr_points,
                                    vector<point>& intersec) {
    int64_t int64_delta = (int64_t) delta;

    for (size_t i = 0; i < ts; i++) {
        for (size_t j = 0; j < tr; j++) {            
            int64_t total_dist = 0;

            for (size_t k = 0; k < d; k++) {
                int64_t sndr_point = ((int64_t) sndr_points[i][k]);
                int64_t rcvr_point = ((int64_t) rcvr_points[j][k]);

                int64_t dist = std::abs(sndr_point - rcvr_point);
                if (dist > int64_delta) {
                    total_dist = int64_delta + 1;
                    break;
                }

                total_dist += dist;
            }

            if (total_dist <= int64_delta) {
                intersec.push_back(sndr_points[i]);
                break;
            }
        }
    }
}

bool is_intersec_correct(AES& aes, std::vector<point>& intersec, std::vector<point>& expected_intersec) {
    
    if (intersec.size() != expected_intersec.size()) {
        return false;
    }

    unordered_map<block, bool> intersec_map;

    for (size_t i = 0; i < intersec.size(); i++) {
        intersec_map.insert(std::make_pair(sparse_comp::hash_point(aes, intersec[i]), true));
    }

    for (size_t i = 0; i < expected_intersec.size(); i++) {
        if (!intersec_map.contains(sparse_comp::hash_point(aes, expected_intersec[i]))) {
            return false;
        }
    }

    return true;

}
/*

TEST_CASE("Fuzzy L_inf : simple test 1 (t_s=2, t_r=2, d=2, delta=10, ssp=40)","[splinf][simple]")
{
    constexpr size_t TS = 2;
    constexpr size_t TR = 2;
    constexpr size_t D = 2;
    constexpr size_t DELTA = 10;
    constexpr size_t ssp = 40;

    auto socks = LocalAsyncSocket::makePair();

    PRNG senderPRNG = PRNG(block(50, 6));
    PRNG receiverPRNG = PRNG(block(37, 44));
    AES aes = AES(block(311, 127));

    std::array<point, TS> *senderPoints = new std::array<point, TS>();
    std::array<point, TR> *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    uint32_t c[point::MAX_DIM];
    uint32_t c1[point::MAX_DIM];
    uint32_t c2[point::MAX_DIM];
    c[0] = 10000;
    c[1] = 25000;
    c1[0] = 18;
    c1[1] = 25;
    c2[0] = 25;
    c2[1] = 25;

    senderPoints->at(0) = point(D, c1);
    senderPoints->at(1) = point(D, c);
    receiverPoints->at(0) = point(D, c);
    receiverPoints->at(1) = point(D, c2);

    sparse_comp::fuzzy_l1::Sender<TR, TS, D, DELTA, ssp> fuzzyL1Sender(senderPRNG, aes);
    sparse_comp::fuzzy_l1::Receiver<TS, TR, D, DELTA, ssp> fuzzyL1Recvr(receiverPRNG, aes);

    auto sender_proto = fuzzyL1Sender.send(socks[0], *senderPoints);
    auto receiver_proto = fuzzyL1Recvr.receive(socks[1], *receiverPoints, intersec);

    sync_wait(when_all_ready(std::move(sender_proto), std::move(receiver_proto)));

    vector<point> expected_intersec;

    safer_expected_l1_intersect<TR, TS, D, DELTA>(aes,
                                    *receiverPoints,
                                    *senderPoints,
                                    expected_intersec);

    delete senderPoints;
    delete receiverPoints;

    REQUIRE(is_intersec_correct(aes, intersec, expected_intersec));

}

TEST_CASE("Fuzzy L_inf : simple test 2 (t_s=2, t_r=2, d=2, delta=10, ssp=40)","[splinf][simple]")
{
    constexpr size_t TS = 2;
    constexpr size_t TR = 2;
    constexpr size_t D = 2;
    constexpr size_t DELTA = 10;
    constexpr size_t ssp = 40;

    auto socks = LocalAsyncSocket::makePair();

    PRNG senderPRNG = PRNG(block(50, 6));
    PRNG receiverPRNG = PRNG(block(37, 44));
    AES aes = AES(block(311, 127));

    std::array<point, TS> *senderPoints = new std::array<point, TS>();
    std::array<point, TR> *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    uint32_t c1[point::MAX_DIM];
    uint32_t c2[point::MAX_DIM];
    uint32_t c3[point::MAX_DIM];
    uint32_t c4[point::MAX_DIM];
    c1[0] = 10005;
    c1[1] = 25000;
    c2[0] = 18;
    c2[1] = 25;
    c3[0] = 25;
    c3[1] = 22;
    c4[0] = 10000;
    c4[1] = 24994;

    senderPoints->at(0) = point(D, c1);
    senderPoints->at(1) = point(D, c2);
    receiverPoints->at(0) = point(D, c3);
    receiverPoints->at(1) = point(D, c4);

    sparse_comp::fuzzy_l1::Sender<TR, TS, D, DELTA, ssp> fuzzyL1Sender(senderPRNG, aes);
    sparse_comp::fuzzy_l1::Receiver<TS, TR, D, DELTA, ssp> fuzzyL1Recvr(receiverPRNG, aes);

    auto sender_proto = fuzzyL1Sender.send(socks[0], *senderPoints);
    auto receiver_proto = fuzzyL1Recvr.receive(socks[1], *receiverPoints, intersec);

    sync_wait(when_all_ready(std::move(sender_proto), std::move(receiver_proto)));

    vector<point> expected_intersec;

    safer_expected_l1_intersect<TR, TS, D, DELTA>(aes,
                                    *receiverPoints,
                                    *senderPoints,
                                    expected_intersec);

    delete senderPoints;
    delete receiverPoints;

    REQUIRE(is_intersec_correct(aes, intersec, expected_intersec));

}
*/
TEST_CASE("Fuzzy L_1 : random test (t_s=2, t_r=2, d=2, delta=10, ssp=40)","[splinf][random]")
{
    constexpr size_t TS = 2;
    constexpr size_t TR = 2;
    constexpr size_t D = 2;
    constexpr size_t DELTA = 10;
    constexpr size_t ssp = 40;
    size_t target_matching_points = 1;

    auto socks = LocalAsyncSocket::makePair();

    block seed = block(9536629126107612350ULL,2721317164341290561ULL);
    PRNG senderPRNG = PRNG(block(742130310438916676ULL, 11803924226990735076ULL));
    PRNG receiverPRNG = PRNG(block(2457938039974938056ULL, 17910068785450354990ULL));
    AES aes = AES(block(14034463513942181890ULL, 16276202269246990858ULL));

    std::array<point, TS> *senderPoints = new std::array<point, TS>();
    std::array<point, TR> *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    gen_constrained_rand_inputs<TR, TS, D, DELTA>(seed,
                                                target_matching_points,
                                                *receiverPoints,
                                                *senderPoints);

    sparse_comp::fuzzy_l1::Sender<TR, TS, D, DELTA, ssp> fuzzyL1Sender(senderPRNG, aes);
    sparse_comp::fuzzy_l1::Receiver<TS, TR, D, DELTA, ssp> fuzzyL1Recvr(receiverPRNG, aes);

    auto sender_proto = fuzzyL1Sender.send(socks[0], *senderPoints);
    auto receiver_proto = fuzzyL1Recvr.receive(socks[1], *receiverPoints, intersec);

    sync_wait(when_all_ready(std::move(sender_proto), std::move(receiver_proto)));

    std::vector<point> expected_intersec;

    safer_expected_l1_intersect<TR, TS, D, DELTA>(aes,
                                    *receiverPoints,
                                    *senderPoints,
                                    expected_intersec);
    REQUIRE(expected_intersec.size() == target_matching_points);

    delete senderPoints;
    delete receiverPoints;

    REQUIRE(is_intersec_correct(aes, intersec, expected_intersec));
    REQUIRE(intersec.size() == target_matching_points);

}

TEST_CASE("Fuzzy L_inf : random test (t_s=2, t_r=2, d=6, delta=10, ssp=40)","[splinf][random]")
{
    constexpr size_t TS = 2;
    constexpr size_t TR = 2;
    constexpr size_t D = 6;
    constexpr size_t DELTA = 10;
    constexpr size_t ssp = 40;
    size_t target_matching_points = 1;

    auto socks = LocalAsyncSocket::makePair();

    block seed = block(9536629126107612350ULL,2721317164341290561ULL);
    PRNG senderPRNG = PRNG(block(742130310438916676ULL, 11803924226990735076ULL));
    PRNG receiverPRNG = PRNG(block(2457938039974938056ULL, 17910068785450354990ULL));
    AES aes = AES(block(14034463513942181890ULL, 16276202269246990858ULL));

    std::array<point, TS> *senderPoints = new std::array<point, TS>();
    std::array<point, TR> *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    gen_constrained_rand_inputs<TR, TS, D, DELTA>(seed,
                                                target_matching_points,
                                                *receiverPoints,
                                                *senderPoints);

    sparse_comp::fuzzy_l1::Sender<TR, TS, D, DELTA, ssp> fuzzyL1Sender(senderPRNG, aes);
    sparse_comp::fuzzy_l1::Receiver<TS, TR, D, DELTA, ssp> fuzzyL1Recvr(receiverPRNG, aes);

    auto sender_proto = fuzzyL1Sender.send(socks[0], *senderPoints);
    auto receiver_proto = fuzzyL1Recvr.receive(socks[1], *receiverPoints, intersec);

    sync_wait(when_all_ready(std::move(sender_proto), std::move(receiver_proto)));

    std::vector<point> expected_intersec;

    expected_l1_intersect<TR, TS, D, DELTA>(aes,
                                    *receiverPoints,
                                    *senderPoints,
                                    expected_intersec);
    REQUIRE(expected_intersec.size() == target_matching_points);


    delete senderPoints;
    delete receiverPoints;

    REQUIRE(is_intersec_correct(aes, intersec, expected_intersec));
    REQUIRE(intersec.size() == target_matching_points);
}

TEST_CASE("Fuzzy L_inf : random test (t_s=10, t_r=10, d=2, delta=10, ssp=40)","[splinf][random]")
{
    constexpr size_t TS = 10;
    constexpr size_t TR = 10;
    constexpr size_t D = 2;
    constexpr size_t DELTA = 10;
    constexpr size_t ssp = 40;
    size_t target_matching_points = 3;

    auto socks = LocalAsyncSocket::makePair();

    block seed = block(9536629026107651350ULL,2724119864341290560ULL);
    PRNG senderPRNG = PRNG(block(742130310438916676ULL, 11803924226990735076ULL));
    PRNG receiverPRNG = PRNG(block(2457938039974938056ULL, 17910068785450354990ULL));
    AES aes = AES(block(14034463513942181890ULL, 16276202269246990858ULL));

    std::array<point, TS> *senderPoints = new std::array<point, TS>();
    std::array<point, TR> *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    gen_constrained_rand_inputs<TR, TS, D, DELTA>(seed,
                                                  target_matching_points,
                                                  *receiverPoints,
                                                  *senderPoints);

    sparse_comp::fuzzy_l1::Sender<TR, TS, D, DELTA, ssp> fuzzyL1Sender(senderPRNG, aes);
    sparse_comp::fuzzy_l1::Receiver<TS, TR, D, DELTA, ssp> fuzzyL1Recvr(receiverPRNG, aes);

    auto sender_proto = fuzzyL1Sender.send(socks[0], *senderPoints);
    auto receiver_proto = fuzzyL1Recvr.receive(socks[1], *receiverPoints, intersec);

    sync_wait(when_all_ready(std::move(sender_proto), std::move(receiver_proto)));

    std::vector<point> expected_intersec;

    expected_l1_intersect<TR, TS, D, DELTA>(aes,
                                    *receiverPoints,
                                    *senderPoints,
                                    expected_intersec);
    REQUIRE(expected_intersec.size() == target_matching_points);

    delete senderPoints;
    delete receiverPoints;

    REQUIRE(is_intersec_correct(aes, intersec, expected_intersec));
    REQUIRE(intersec.size() == target_matching_points);

}

TEST_CASE("Fuzzy L_inf : random test (t_s=10, t_r=10, d=6, delta=10, ssp=40)","[splinf][random]")
{
    constexpr size_t TS = 10;
    constexpr size_t TR = 10;
    constexpr size_t D = 6;
    constexpr size_t DELTA = 10;
    constexpr size_t ssp = 40;
    size_t target_matching_points = 3;

    auto socks = LocalAsyncSocket::makePair();

    block seed = block(9536629026107651350ULL,2724119864341290560ULL);
    PRNG senderPRNG = PRNG(block(742130310438916676ULL, 11803924226990735076ULL));
    PRNG receiverPRNG = PRNG(block(2457938039974938056ULL, 17910068785450354990ULL));
    AES aes = AES(block(14034463513942181890ULL, 16276202269246990858ULL));

    std::array<point, TS> *senderPoints = new std::array<point, TS>();
    std::array<point, TR> *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    gen_constrained_rand_inputs<TR, TS, D, DELTA>(seed,
                                                  target_matching_points,
                                                  *receiverPoints,
                                                  *senderPoints);

    sparse_comp::fuzzy_l1::Sender<TR, TS, D, DELTA, ssp> fuzzyL1Sender(senderPRNG, aes);
    sparse_comp::fuzzy_l1::Receiver<TS, TR, D, DELTA, ssp> fuzzyL1Recvr(receiverPRNG, aes);

    auto sender_proto = fuzzyL1Sender.send(socks[0], *senderPoints);
    auto receiver_proto = fuzzyL1Recvr.receive(socks[1], *receiverPoints, intersec);

    sync_wait(when_all_ready(std::move(sender_proto), std::move(receiver_proto)));

    std::vector<point> expected_intersec;

    expected_l1_intersect<TR, TS, D, DELTA>(aes,
                                    *receiverPoints,
                                    *senderPoints,
                                    expected_intersec);
    REQUIRE(expected_intersec.size() == target_matching_points);

    delete senderPoints;
    delete receiverPoints;

    REQUIRE(is_intersec_correct(aes, intersec, expected_intersec));
    REQUIRE(intersec.size() == target_matching_points);

}

TEST_CASE("Fuzzy L_inf : safer random test (t_s=10, t_r=10, d=2, delta=10, ssp=40)","[splinf][random]")
{
    constexpr size_t TS = 10;
    constexpr size_t TR = 10;
    constexpr size_t D = 2;
    constexpr size_t DELTA = 10;
    constexpr size_t ssp = 40;
    size_t target_matching_points = 3;

    auto socks = LocalAsyncSocket::makePair();

    block seed = block(9532629066107651350ULL,2724141865341230560ULL);
    PRNG senderPRNG = PRNG(block(742130310438916676ULL, 11803924226990735076ULL));
    PRNG receiverPRNG = PRNG(block(2457938039974938056ULL, 17910068785450354990ULL));
    AES aes = AES(block(14034463513942181890ULL, 16276202269246990858ULL));

    std::array<point, TS> *senderPoints = new std::array<point, TS>();
    std::array<point, TR> *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    gen_constrained_rand_inputs<TR, TS, D, DELTA>(seed,
                                                  target_matching_points,
                                                  *receiverPoints,
                                                  *senderPoints);

    sparse_comp::fuzzy_l1::Sender<TR, TS, D, DELTA, ssp> fuzzyL1Sender(senderPRNG, aes);
    sparse_comp::fuzzy_l1::Receiver<TS, TR, D, DELTA, ssp> fuzzyL1Recvr(receiverPRNG, aes);

    auto sender_proto = fuzzyL1Sender.send(socks[0], *senderPoints);
    auto receiver_proto = fuzzyL1Recvr.receive(socks[1], *receiverPoints, intersec);

    sync_wait(when_all_ready(std::move(sender_proto), std::move(receiver_proto)));

    std::vector<point> expected_intersec;

    safer_expected_l1_intersect<TR, TS, D, DELTA>(aes,
                                    *receiverPoints,
                                    *senderPoints,
                                    expected_intersec);
    REQUIRE(expected_intersec.size() == target_matching_points);

    delete senderPoints;
    delete receiverPoints;

    REQUIRE(is_intersec_correct(aes, intersec, expected_intersec));
    REQUIRE(intersec.size() == target_matching_points);

}

TEST_CASE("Fuzzy L_inf : safer random test (t_s=10, t_r=10, d=6, delta=10, ssp=40)","[splinf][random]")
{
    constexpr size_t TS = 10;
    constexpr size_t TR = 10;
    constexpr size_t D = 6;
    constexpr size_t DELTA = 10;
    constexpr size_t ssp = 40;
    size_t target_matching_points = 3;

    auto socks = LocalAsyncSocket::makePair();

    block seed = block(9532629066107651350ULL,2724141865341230560ULL);
    PRNG senderPRNG = PRNG(block(742130310438916676ULL, 11803924226990735076ULL));
    PRNG receiverPRNG = PRNG(block(2457938039974938056ULL, 17910068785450354990ULL));
    AES aes = AES(block(14034463513942181890ULL, 16276202269246990858ULL));

    std::array<point, TS> *senderPoints = new std::array<point, TS>();
    std::array<point, TR> *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    gen_constrained_rand_inputs<TR, TS, D, DELTA>(seed,
                                                  target_matching_points,
                                                  *receiverPoints,
                                                  *senderPoints);

    sparse_comp::fuzzy_l1::Sender<TR, TS, D, DELTA, ssp> fuzzyL1Sender(senderPRNG, aes);
    sparse_comp::fuzzy_l1::Receiver<TS, TR, D, DELTA, ssp> fuzzyL1Recvr(receiverPRNG, aes);

    auto sender_proto = fuzzyL1Sender.send(socks[0], *senderPoints);
    auto receiver_proto = fuzzyL1Recvr.receive(socks[1], *receiverPoints, intersec);

    sync_wait(when_all_ready(std::move(sender_proto), std::move(receiver_proto)));

    std::vector<point> expected_intersec;

    safer_expected_l1_intersect<TR, TS, D, DELTA>(aes,
                                    *receiverPoints,
                                    *senderPoints,
                                    expected_intersec);
    REQUIRE(expected_intersec.size() == target_matching_points);

    delete senderPoints;
    delete receiverPoints;

    REQUIRE(is_intersec_correct(aes, intersec, expected_intersec));
    REQUIRE(intersec.size() == target_matching_points);

}

TEST_CASE("Fuzzy L_inf : safer random test (t_s=256, t_r=256, d=2, delta=10, ssp=40)","[splinf][random]")
{
    constexpr size_t TS = 256;
    constexpr size_t TR = 256;
    constexpr size_t D = 2;
    constexpr size_t DELTA = 10;
    constexpr size_t ssp = 40;
    size_t target_matching_points = 3;

    auto socks = LocalAsyncSocket::makePair();

    block seed = block(4337051091801073793ULL,11168696276776517519ULL);
    PRNG senderPRNG = PRNG(block(742130310438916676ULL, 11803924226990735076ULL));
    PRNG receiverPRNG = PRNG(block(2457938039974938056ULL, 17910068785450354990ULL));
    AES aes = AES(block(14034463513942181890ULL, 16276202269246990858ULL));

    std::array<point, TS> *senderPoints = new std::array<point, TS>();
    std::array<point, TR> *receiverPoints = new std::array<point, TR>();
    std::vector<point> intersec;

    gen_constrained_rand_inputs<TR, TS, D, DELTA>(seed,
                                                  target_matching_points,
                                                  *receiverPoints,
                                                  *senderPoints);

    sparse_comp::fuzzy_l1::Sender<TR, TS, D, DELTA, ssp> fuzzyL1Sender(senderPRNG, aes);
    sparse_comp::fuzzy_l1::Receiver<TS, TR, D, DELTA, ssp> fuzzyL1Recvr(receiverPRNG, aes);

    auto sender_proto = fuzzyL1Sender.send(socks[0], *senderPoints);
    auto receiver_proto = fuzzyL1Recvr.receive(socks[1], *receiverPoints, intersec);

    sync_wait(when_all_ready(std::move(sender_proto), std::move(receiver_proto)));

    std::vector<point> expected_intersec;

    safer_expected_l1_intersect<TR, TS, D, DELTA>(aes,
                                    *receiverPoints,
                                    *senderPoints,
                                    expected_intersec);
    REQUIRE(expected_intersec.size() == target_matching_points);

    delete senderPoints;
    delete receiverPoints;

    REQUIRE(is_intersec_correct(aes, intersec, expected_intersec));
    REQUIRE(intersec.size() == target_matching_points);

}

