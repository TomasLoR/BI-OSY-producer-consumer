#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <compare>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"

char __libc_single_threaded = 0;
using namespace std;
#endif /* __PROGTEST__ */

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
struct wrapSolver {
    AProgtestSolver solver;
    map<AProblemPack, pair<size_t, size_t>> insertedProblems;
    //                     ^cnt    ^min
};

class COptimizer {
public:
    static bool usingProgtestSolver(void) {
        return true;
    }

    static void checkAlgorithmMin(APolygon p) {
        // dummy implementation if usingProgtestSolver() returns true
    }

    static void checkAlgorithmCnt(APolygon p) {
        // dummy implementation if usingProgtestSolver() returns true
    }

    void start(int threadCount);

    void stop(void);

    void addCompany(ACompany company);

    // moje metody
    void createCntSolver(void) {
        AProgtestSolver solver = createProgtestCntSolver();
        wrapSolver solverWrap = {solver, map<AProblemPack, pair<size_t, size_t>>{}};
        solversCnt.emplace_back(solverWrap);
    }

    void createMinSolver(void) {
        AProgtestSolver solver = createProgtestMinSolver();
        wrapSolver solverWrap = {solver, map<AProblemPack, pair<size_t, size_t>>{}};
        solversMin.emplace_back(solverWrap);
    }

    // ukladani problempacků a jejich poctu problémů do globálniho bufferu navíc
    void updateWrapProblems (const string &type, const AProblemPack pack) {
        if (type == "cnt") {
            if (solversCnt[0].insertedProblems.count(pack) == 0) {
                solversCnt[0].insertedProblems[pack] = {1, 0};
            } else {
                solversCnt[0].insertedProblems[pack].first++;
            }
        } else {
            if (solversMin[0].insertedProblems.count(pack) == 0) {
                solversMin[0].insertedProblems[pack] = {0, 1};
            } else {
                solversMin[0].insertedProblems[pack].second++;
            }
        }
    }

    void moveToMix (const wrapSolver &solver) {
        unique_lock<mutex> lock(mtxMix);
        solversMix.emplace_back(solver);
        lock.unlock();
        cv1.notify_all();
    }

    void solveSolver(wrapSolver &solverW) {
        solverW.solver->solve();
        for (const auto &x : solverW.insertedProblems) {

            unique_lock<mutex> lockSolved(mtxSolved);
            solved[x.first].first -= x.second.first;
            solved[x.first].second -= x.second.second;

            // zkonstroluji jesli je nejaky pack solved diky poslednimu zavolani .solve()
            if (solved[x.first].first == 0 && solved[x.first].second == 0) {
                cv2.notify_all();
            }
            lockSolved.unlock();
        }
    }

// private:
    vector<thread> workers;
    vector<thread> receivers;
    vector<thread> delivers;
    vector<ACompany> companies;
    vector<queue<AProblemPack>> problems;
    map<AProblemPack, pair<size_t, size_t>> solved;

    vector<wrapSolver> solversCnt;
    vector<wrapSolver> solversMin;
    vector<wrapSolver> solversMix;

    mutex mtxCnt;
    mutex mtxMin;
    mutex mtxMix;
    mutex mtxProblems;
    mutex mtxSolved;

    atomic<bool> receiversDown{false};
    atomic<bool> workersDown{false};

    condition_variable cv1;
    condition_variable cv2;
};

void COptimizer::addCompany(ACompany company) {
    companies.emplace_back(company);
}

void COptimizer::stop(void) {

    for (auto& receiver : receivers) {
        receiver.join();
    }
//    receiversDown.store(true);
//    unique_lock<mutex> lockBool(mtxBool);
    receiversDown = true;
//    unique_lock<mutex> lockBool(mtxBool);
//    printf("Receivers are down\n");
    unique_lock<mutex> lockMix(mtxMix);
    cv1.notify_all();
    lockMix.unlock();
//    printf("I notified\n");


    for (auto& worker : workers) {
        worker.join();
    }
    workersDown = true;
//    printf("Workers are down\n");
    unique_lock<mutex> lockProblems(mtxProblems);
    cv2.notify_all();
    lockProblems.unlock();

//    printf("I notified 2\n");

    for (auto& deliver : delivers) {
        deliver.join();
    }


}

void COptimizer::start(int threadCount) {
    problems.resize(companies.size());

    // Vytvareni workerů
    for (int i = 0; i < threadCount; i++) {
        workers.emplace_back([this]() {
//            printf("Worker IN\n");
            while(true) {
//                printf("Working...\n");
                unique_lock<mutex> lockMix(mtxMix);
                cv1.wait(lockMix, [this] {/*printf("Bool of receiversDown: %d\n", receiversDown.load());*/ return (!solversMix.empty() || receiversDown);});
//                printf("I am out\n");
                if (receiversDown && solversMix.empty()) {
                    unique_lock<mutex> lockCnt(mtxCnt);
                    if (!solversCnt.empty()) {
                        wrapSolver solverW = std::move(solversCnt.back());
                        solversCnt.pop_back();
                        solveSolver(solverW);
                    }
//                    lockCnt.unlock();

                    unique_lock<mutex> lockMin(mtxMin);
                    if (!solversMin.empty()) {
                        wrapSolver solverW = std::move(solversMin.back());
                        solversMin.pop_back();
                        solveSolver(solverW);
                    }
//                    lockMin.unlock();

//                    lockMix.unlock();
                    cv1.notify_all();
                    break;
                }

                wrapSolver solverW = std::move(solversMix.back());
                solversMix.pop_back();
                lockMix.unlock();
                solveSolver(solverW);
            }
//            printf("Worker OUT\n");
        });
    }

    // cyklus vytvarejici receivers a delivers
    for (size_t i = 0; i < companies.size(); i++) {

        //receivers
        receivers.emplace_back([this, i]() {

            while (true) {

                AProblemPack pack = companies[i]->waitForPack();
                if (pack == nullptr) {
                    break;
                }
//                printf("Prevzal jsem balik\n");

                unique_lock<mutex> lockSolved(mtxSolved);
                solved[pack] = {pack->m_ProblemsCnt.size(), pack->m_ProblemsMin.size()};
                lockSolved.unlock();

                unique_lock<mutex> lockProblems(mtxProblems);
                problems[i].emplace(pack);
                lockProblems.unlock();

                unique_lock<mutex> lockCnt(mtxCnt);
                if (solversCnt.empty()) {
                    createCntSolver();
                }

                for (const auto& prob : pack->m_ProblemsCnt) {

                    solversCnt[0].solver->addPolygon(prob);
                    updateWrapProblems("cnt", pack);

                    if (!solversCnt[0].solver->hasFreeCapacity()) {
                        moveToMix(solversCnt[0]);
                        solversCnt.clear();
                        createCntSolver();
                    }


                }
                lockCnt.unlock();

                unique_lock<mutex> lockMin(mtxMin);
                if (solversMin.empty()) {
                    createMinSolver();
                }

                for (const auto& prob : pack->m_ProblemsMin) {

                    solversMin[0].solver->addPolygon(prob);
                    updateWrapProblems("min", pack);

                    if (!solversMin[0].solver->hasFreeCapacity()) {
                        moveToMix(solversMin[0]);
                        solversMin.clear();
                        createMinSolver();
                    }
                }
                lockMin.unlock();
            }
        });

        // delivers
        delivers.emplace_back([this, i]() {

            while (true) {
//                printf("Problems size: %zu\n", problems[i].size());
                unique_lock<mutex> lockProblems(mtxProblems);
                if (problems[i].empty() && receiversDown) {
//                    lockProblems.unlock();
                    break;
                }

                // fixed busy waiting

                cv2.wait(lockProblems, [this, i] {
                    if (workersDown) {
                        return true;
                    }

                    if (problems[i].empty()) {
                        return false;
                    }
                    AProblemPack key = problems[i].front();

                    unique_lock<mutex> lockSolved(mtxSolved);
                    if ((solved.count(key) != 0 && solved[key].first == 0 && solved[key].second == 0)) {
                        return true;
                    }
                    return false;
                });

                if (problems[i].empty() && receiversDown) {
                    break;
                }

                if (!problems[i].empty()) {
                    AProblemPack key = problems[i].front();
                    unique_lock<mutex> lockSolved(mtxSolved);
                    if (solved[key].first == 0 && solved[key].second == 0) {
                        companies[i]->solvedPack(key);
                        problems[i].pop();
                    }

                    lockSolved.unlock();
                }

                lockProblems.unlock();
            }
        });

    }

}
// TODO: COptimizer implementation goes here
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__

int main(void) {
//    int i = 0;
//    while (true) {
//        printf("Start\n");

        COptimizer optimizer;
        ACompanyTest company = std::make_shared<CCompanyTest>();
        optimizer.addCompany(company);
        ACompanyTest company2 = std::make_shared<CCompanyTest>();
        optimizer.addCompany(company2);
        optimizer.start(1);
        optimizer.stop();
        if (!company->allProcessed())
            throw std::logic_error("(some) problems were not correctly processsed");
//        printf("End\n");
//        i++;
//        printf("%d\n", i);
//    }
    return 0;
}

#endif /* __PROGTEST__ */
