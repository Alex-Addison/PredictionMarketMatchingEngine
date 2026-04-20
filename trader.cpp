#include <quickfix/Application.h>
#include <quickfix/Values.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Session.h>

#include <iostream>
#include <iomanip>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <x86intrin.h> 
#include <atomic>

const int MAX_ORDERS = 100000;

class TraderApp : public FIX::Application {
private:
    std::mutex mtx;
    std::condition_variable cv;
    bool loggedOn = false;
    FIX::SessionID activeSessionID;

    // atomic arrays for speed and thread safety
    std::atomic<uint64_t> orderTimestamps[MAX_ORDERS];
    std::atomic<uint64_t> latencyTicks[MAX_ORDERS];
    std::atomic<char> orderStatuses[MAX_ORDERS];

    // for rdtsc timing division
    double clockrate = 0.0;

    // used to sync trader 1 and 2
    std::mutex syncMtx;
    std::condition_variable syncCv;
    bool syncComplete = false;
public:
    std::string myTraderID;
    std::string myPrefix;

private:
    // get clockrate by calling rdtsc around a sleep call
    void calibrateClockrate() {
        auto start_time = std::chrono::high_resolution_clock::now();
        uint64_t start_tsc = __rdtsc();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        uint64_t end_tsc = __rdtsc();
        auto end_time = std::chrono::high_resolution_clock::now();
        auto elapsed_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        clockrate = (static_cast<double>(end_tsc - start_tsc) * 1e9) / elapsed_nanos;
        std::cout << "Clockrate: " << (clockrate / 1e9) << " GHz" << std::endl;
    }

    // helper for syncing with other trader (allows multiple syncs)
    void waitForSync() {
        std::unique_lock<std::mutex> lock(syncMtx);
        syncCv.wait(lock, [this]{ return syncComplete; });
        syncComplete = false; 
    }

public:
    TraderApp() {
        // clear arrays
        for(int i=0; i<MAX_ORDERS; ++i) {
            orderTimestamps[i] = 0;
            latencyTicks[i] = 0;
            orderStatuses[i] = ' ';
        }
        // sets clockrate
        calibrateClockrate(); 
    }

    void onCreate(const FIX::SessionID&) override {}
    // set trader #
    void onLogon(const FIX::SessionID& sessionID) override {
        std::unique_lock<std::mutex> lock(mtx);
        activeSessionID = sessionID;
        myTraderID = sessionID.getSenderCompID().getString();
        myPrefix = (myTraderID == "TRADER1") ? "TRD1_" : "TRD2_";
        loggedOn = true;
        std::cout << "Logon: " << myTraderID << std::endl;
        cv.notify_all(); 
    }

    void onLogout(const FIX::SessionID&) override {}
    void toAdmin(FIX::Message&, const FIX::SessionID&) override {}
    void toApp(FIX::Message&, const FIX::SessionID&) throw(FIX::DoNotSend) override {}
    void fromAdmin(const FIX::Message&, const FIX::SessionID&) throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) override {}

    // wait until logged on
    void waitForLogon() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]{ return loggedOn; });
    }

    // sends an order to the gateway
    void sendOrder(const FIX::SessionID& sessionID, int index, std::string symbol, char side, int qty, double price) {
        FIX::Message order;
        order.getHeader().setField(FIX::BeginString("FIXT.1.1"));
        order.getHeader().setField(FIX::MsgType(FIX::MsgType_NewOrderSingle));
        
        order.setField(FIX::ClOrdID(myPrefix + std::to_string(index)));
        order.setField(FIX::Symbol(symbol));
        order.setField(FIX::Side(side)); 
        order.setField(FIX::OrderQty(qty));
        order.setField(FIX::Price(price));
        order.setField(FIX::OrdType(FIX::OrdType_LIMIT));

        // log the cycle is was sent
        orderTimestamps[index] = __rdtsc();
        FIX::Session::sendToTarget(order, sessionID);
    }

    // TEST CASES

    // check that invalid symbol rejected
    void invalidSymbolTest() {
        if (myTraderID != "TRADER1") return;
        std::cout << "INVALID SYMBOL TEST" << std::endl;
        sendOrder(activeSessionID, 20002, "NOT-A-MARKET", FIX::Side_BUY, 10, 0.50);
    }

    // check that 1 report sent for each booked order (non-matched)
    void reportCountTest() {
        if (myTraderID != "TRADER1") return;
        std::cout << "REPORT COUNT TEST" << std::endl;
        for (int i = 0; i < 5; i++) {
            sendOrder(activeSessionID, 20003 + i, "KXFED-26DEC-5.00", FIX::Side_BUY, 10, 0.10);
        }
    }

    
    void matchingTest() {
        std::cout << "SYNCING WITH OTHER TRADER" << std::endl;
        if (myTraderID == "TRADER1") sendOrder(activeSessionID, 20001, "SYNC-MARKET", FIX::Side_SELL, 100, 0.50);
        else { std::this_thread::sleep_for(std::chrono::seconds(1)); sendOrder(activeSessionID, 20001, "SYNC-MARKET", FIX::Side_BUY, 100, 0.50); }
        waitForSync();
        
        std::cout << "MATCHING TRADE TEST" << std::endl;
        for (int i = 0; i < 10000; i++) {
            char side = (myTraderID == "TRADER1") ? FIX::Side_BUY : FIX::Side_SELL;
            sendOrder(activeSessionID, i, (i % 2 == 0) ? "KXFED-26DEC-5.00" : "KXWEATHER-MIA-85", side, 10, 0.500);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    void depthTest() {
        std::cout << "SYNCING WITH OTHER TRADER" << std::endl;
        if (myTraderID == "TRADER1") sendOrder(activeSessionID, 20000, "SYNC-MARKET", FIX::Side_SELL, 100, 0.50);
        else { std::this_thread::sleep_for(std::chrono::seconds(1)); sendOrder(activeSessionID, 20000, "SYNC-MARKET", FIX::Side_BUY, 100, 0.50); }
        waitForSync();
        
        std::cout << "DEPTH TEST" << std::endl;
        for (int i = 0; i < 10000; i++) {
            int tick_step = i / 20; 
            double price = (myTraderID == "TRADER1") ? 0.050 + (tick_step * 0.001) : 0.950 - (tick_step * 0.001);
            char side = (myTraderID == "TRADER1") ? FIX::Side_BUY : FIX::Side_SELL;
            sendOrder(activeSessionID, 10000 + i, (i % 2 == 0) ? "KXFED-26DEC-5.00" : "KXWEATHER-MIA-85", side, 10, price);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    void horizontalScaleTest() {
        if (myTraderID != "TRADER1") return; // TRADER 2 does nothing during this test

        std::cout << "\nSCALING TEST: single node (10k Orders -> Engine 1)" << std::endl;
        
        // single node: 10,000 orders hitting only Engine 1
        for (int i = 0; i < 10000; i++) {
            // Alternate buy/sell they clear
            char side = (i % 2 == 0) ? FIX::Side_BUY : FIX::Side_SELL;
            
            // idx 30000 to 39999
            sendOrder(activeSessionID, 30000 + i, "KXFED-26DEC-5.00", side, 10, 0.50);
            
            std::this_thread::sleep_for(std::chrono::microseconds(10)); 
        }

        // Wait to let queue flush
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::cout << "SCALING TEST: two nodes (10k Orders -> Engine 1 & 2)" << std::endl;
        
        for (int i = 0; i < 10000; i++) {
            std::string targetSymbol = ((i / 2) % 2 == 0) ? "KXFED-26DEC-5.00" : "KXWEATHER-MIA-85";
            char side = (i % 2 == 0) ? FIX::Side_BUY : FIX::Side_SELL;

            // idx 40000 to 49999
            sendOrder(activeSessionID, 40000 + i, targetSymbol, side, 10, 0.50);
            
            std::this_thread::sleep_for(std::chrono::microseconds(10)); 
        }
    }
    void fromApp(const FIX::Message& message, const FIX::SessionID&) 
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override {
        uint64_t end_tick = __rdtsc();
        if (message.getHeader().getField(FIX::FIELD::MsgType) == FIX::MsgType_ExecutionReport) {
            std::string clOrdID = message.getField(FIX::FIELD::ClOrdID);
            char status = message.getField(FIX::FIELD::OrdStatus)[0];
            
            int index = std::stoi(clOrdID.substr(5));

            // sync indixies are 20000, 20001, 50000
            if ((index == 20000 || index == 20001 || index == 50000) && (status == '1' || status == '2')) {
                std::unique_lock<std::mutex> lock(syncMtx);
                syncComplete = true;
                syncCv.notify_all();
            }

            orderStatuses[index] = status;
            uint64_t start_tick = orderTimestamps[index].exchange(0);
            if (start_tick != 0) latencyTicks[index] = end_tick - start_tick;
        }
    }

    // REPORTING FUNCTIONS

    void printRejectionReport(int index) {
        if (myTraderID != "TRADER1") return; 
        char status = orderStatuses[index];
        std::cout << "\n--- TEST: INVALID SYMBOL ---" << std::endl;
        std::cout << "Order ID: " << index << std::endl;
        std::cout << "Status: " << (status == '8' ? "REJECTED (Pass)" : "NOT REJECTED (Fail)") << std::endl;
        std::cout << "----------------------------\n" << std::endl;
    }

    void printCountReport(int start, int end, int expected) {
        if (myTraderID != "TRADER1") return; 
        int received = 0;
        for (int i = start; i <= end; i++) {
            if (orderStatuses[i] != ' ') received++;
        }
        std::cout << "\n--- TEST: REPORT COUNT ---" << std::endl;
        std::cout << "Orders Sent: " << expected << std::endl;
        std::cout << "Reports Received: " << received << std::endl;
        std::cout << "Result: " << (received == expected ? "Success" : "Failure") << std::endl;
        std::cout << "--------------------------\n" << std::endl;
    }

    // use for 3 synced tests
    void printLatencySummary(std::string title, int startIdx, int endIdx) {
        uint64_t total_ticks = 0, max_ticks = 0, min_ticks = 0xFFFFFFFFFFFFFFFF;
        int count = 0;
        for (int i = startIdx; i <= endIdx; ++i) {
            uint64_t diff = latencyTicks[i].exchange(0);
            if (diff > 0) {
                total_ticks += diff;
                if (diff > max_ticks) max_ticks = diff;
                if (diff < min_ticks) min_ticks = diff;
                count++;
            }
        }
        if (count == 0){
            return;
        }
        std::cout << "\n--- TEST: " << title << " ---" << std::endl;
        std::cout << "Orders: " << count << std::endl;
        std::cout << "Min: " << std::fixed << std::setprecision(2) << (static_cast<double>(min_ticks) / clockrate * 1000000.0) << " us" << std::endl;
        std::cout << "Max: " << (static_cast<double>(max_ticks) / clockrate * 1000000.0) << " us" << std::endl;
        std::cout << "Avg: " << (static_cast<double>(total_ticks) / count / clockrate * 1000000.0) << " us" << std::endl;
        std::cout << "Total Time: " << (static_cast<double>(total_ticks) / clockrate) << " s" << std::endl;
        std::cout << "---------------------------------\n" << std::endl;
    }
};

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        FIX::SessionSettings settings(argv[1]);
        TraderApp application;
        FIX::FileStoreFactory storeFactory(settings);
        FIX::FileLogFactory logFactory(settings);
        FIX::SocketInitiator initiator(application, storeFactory, settings, logFactory);
        initiator.start();
        application.waitForLogon();
        
        // simple diagnostics
        application.invalidSymbolTest();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        application.printRejectionReport(20002);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        application.reportCountTest();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        application.printCountReport(20003, 20007, 5);
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // scale test
        application.horizontalScaleTest();
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Wait for reports
        application.printLatencySummary("SINGLE NODE (1 Engine)", 30000, 39999);
        application.printLatencySummary("DISTRIBUTED SCALING (2 Engines)", 40000, 49999);

        // matching test
        application.matchingTest();
        std::this_thread::sleep_for(std::chrono::seconds(10));
        application.printLatencySummary("PERFECT MATCHING", 0, 9999);

        // depth test
        application.depthTest();
        std::this_thread::sleep_for(std::chrono::seconds(15));
        application.printLatencySummary("DEPTH TEST", 10000, 19999);
        
        initiator.stop();
    } catch (FIX::Exception& e) { std::cout << "Error: " << e.what() << std::endl; return 1; }
    return 0;
}