#include <quickfix/Application.h>
#include <quickfix/Values.h>
#include <quickfix/ThreadedSocketAcceptor.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Session.h>

#include <iostream>
#include <vector>
#include <array>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

// order struct
struct Order {
    std::string clOrdID;
    int quantity;
    char side;
    FIX::SessionID sessionID; 
};

// circular buffer for each price increment, max of 200 orders
struct PriceLevel {
    Order orders[200]; 
    int head = 0;
    int tail = 0;
    int count = 0;

    void push(const Order& o) {
        if (count < 200) {
            orders[tail] = o;
            tail = (tail + 1) % 200;
            count++;
        }
    }

    Order& front() { return orders[head]; }

    void pop() {
        head = (head + 1) % 200;
        count--;
    }
};

// Orderbook struct, array of PriceLevels
struct OrderBook {
    std::array<PriceLevel, 1001> bids;
    std::array<PriceLevel, 1001> asks;
    
    int bestBidIdx = -1;
    int bestAskIdx = 1001;

    int priceToIdx(double price) {
        return static_cast<int>(price * 1000.0 + 0.5);
    }
};

class MatchingEngine : public FIX::Application {
private:
    std::mutex bookMutex;
    // maps symbol to OrderBook pointer
    std::map<std::string, OrderBook*> marketData;

public:
    MatchingEngine() {
        // heap allocate books for fed and weather symbols
        marketData["KXFED-26DEC-5.00"] = new OrderBook();
        marketData["KXWEATHER-MIA-85"] = new OrderBook();
        // used to sync traders in tests
        marketData["SYNC-MARKET"] = new OrderBook();
    }

    // destructor
    ~MatchingEngine() {
        for (auto& pair : marketData) delete pair.second;
    }

    void onLogon(const FIX::SessionID& sessionID) override {
        std::cout << "Logon: " << sessionID.getSenderCompID().getString() << std::endl;
    }

    // receive message from gateway
    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) 
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override {
        if (message.getHeader().getField(FIX::FIELD::MsgType) == FIX::MsgType_NewOrderSingle) {
            processOrder(message, sessionID);
        }
    }

    // send report back to gateway
    void sendExecutionReport(const FIX::SessionID& sessionID, std::string clOrdID, std::string symbol, char side, int execQty, double execPrice, int leavesQty, char ordStatus, char execType) {
        FIX::Message report;
        report.getHeader().setField(FIX::BeginString("FIXT.1.1"));
        report.getHeader().setField(FIX::MsgType(FIX::MsgType_ExecutionReport));
        
        report.setField(FIX::ClOrdID(clOrdID));
        report.setField(FIX::OrderID("MKT_" + clOrdID));
        report.setField(FIX::ExecID("E_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
        report.setField(FIX::OrdStatus(ordStatus));
        report.setField(FIX::ExecType(execType));
        report.setField(FIX::Symbol(symbol));
        report.setField(FIX::Side(side));
        report.setField(FIX::LeavesQty(leavesQty));
        report.setField(FIX::CumQty(execQty));
        
        if (execQty > 0) {
            report.setField(FIX::LastPx(execPrice));
            report.setField(FIX::LastQty(execQty));
        }
        report.setField(FIX::AvgPx(execPrice));
        report.setField(FIX::OrderQty(execQty + leavesQty));
        report.setField(FIX::TransactTime());
        FIX::Session::sendToTarget(report, sessionID);
    }

    // matching/booking logic for a new order
    void processOrder(const FIX::Message& msg, const FIX::SessionID& sessionID) {
        std::lock_guard<std::mutex> lock(bookMutex);

        std::string symbol = msg.getField(FIX::FIELD::Symbol);
        std::string clOrdID = msg.getField(FIX::FIELD::ClOrdID);

        // ensure symbol is valid
        if (marketData.find(symbol) == marketData.end()) {
            sendExecutionReport(sessionID, clOrdID, symbol, msg.getField(FIX::FIELD::Side)[0], 0, 0, 0, '8', '8');
            return;
        }

        // parse fields
        OrderBook* book = marketData[symbol];
        char side = msg.getField(FIX::FIELD::Side)[0];
        double price = std::stod(msg.getField(FIX::FIELD::Price));
        int remainingQty = std::stoi(msg.getField(FIX::FIELD::OrderQty));
        int pIdx = book->priceToIdx(price);

        if (side == FIX::Side_BUY) {
            // check if we can match with any asks (might partially fill with more than one ask)
            while (remainingQty > 0 && book->bestAskIdx <= pIdx) {
                PriceLevel& level = book->asks[book->bestAskIdx];
                while (remainingQty > 0 && level.count > 0) {
                    Order& resting = level.front();
                    int tradeQty = std::min(remainingQty, resting.quantity);

                    remainingQty -= tradeQty;
                    resting.quantity -= tradeQty;
                    double matchPrice = (double)book->bestAskIdx / 1000.0;

                    sendExecutionReport(sessionID, clOrdID, symbol, side, tradeQty, matchPrice, remainingQty, (remainingQty == 0 ? '2' : '1'), 'F');
                    sendExecutionReport(resting.sessionID, resting.clOrdID, symbol, resting.side, tradeQty, matchPrice, resting.quantity, (resting.quantity == 0 ? '2' : '1'), 'F');

                    if (resting.quantity == 0) level.pop();
                }
                if (level.count == 0) {
                    // update best ask
                    while (book->bestAskIdx < 1000 && book->asks[book->bestAskIdx].count == 0) book->bestAskIdx++;
                    if (book->asks[book->bestAskIdx].count == 0) book->bestAskIdx = 1001;
                }
            }
            if (remainingQty > 0) {
                book->bids[pIdx].push({clOrdID, remainingQty, side, sessionID});
                if (pIdx > book->bestBidIdx) book->bestBidIdx = pIdx;
                sendExecutionReport(sessionID, clOrdID, symbol, side, 0, 0, remainingQty, '0', '0');
            }
        } 
        else { // check if we match with any bids
            while (remainingQty > 0 && book->bestBidIdx >= pIdx) {
                PriceLevel& level = book->bids[book->bestBidIdx];
                while (remainingQty > 0 && level.count > 0) {
                    Order& resting = level.front();
                    int tradeQty = std::min(remainingQty, resting.quantity);

                    remainingQty -= tradeQty;
                    resting.quantity -= tradeQty;
                    double matchPrice = (double)book->bestBidIdx / 1000.0;

                    sendExecutionReport(sessionID, clOrdID, symbol, side, tradeQty, matchPrice, remainingQty, (remainingQty == 0 ? '2' : '1'), 'F');
                    sendExecutionReport(resting.sessionID, resting.clOrdID, symbol, resting.side, tradeQty, matchPrice, resting.quantity, (resting.quantity == 0 ? '2' : '1'), 'F');

                    if (resting.quantity == 0) level.pop();
                }
                if (level.count == 0) {
                    while (book->bestBidIdx > 0 && book->bids[book->bestBidIdx].count == 0) book->bestBidIdx--;
                    if (book->bids[book->bestBidIdx].count == 0) book->bestBidIdx = -1;
                }
            }
            if (remainingQty > 0) {
                book->asks[pIdx].push({clOrdID, remainingQty, side, sessionID});
                if (pIdx < book->bestAskIdx) book->bestAskIdx = pIdx;
                sendExecutionReport(sessionID, clOrdID, symbol, side, 0, 0, remainingQty, '0', '0');
            }
        }
    }

    // FIX boilerplate
    void onCreate(const FIX::SessionID&) override {}
    void onLogout(const FIX::SessionID&) override {}
    void toAdmin(FIX::Message&, const FIX::SessionID&) override {}
    void toApp(FIX::Message&, const FIX::SessionID&) throw(FIX::DoNotSend) override {}
    void fromAdmin(const FIX::Message&, const FIX::SessionID&) throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) override {}
};

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        FIX::SessionSettings settings(argv[1]);
        
        // allocate engine on heap
        auto* application = new MatchingEngine();
        
        FIX::FileStoreFactory storeFactory(settings);
        FIX::FileLogFactory logFactory(settings);
        FIX::ThreadedSocketAcceptor acceptor(*application, storeFactory, settings, logFactory);
        
        acceptor.start();
        std::cout << "Engine: Running" << std::endl;
        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
        
        acceptor.stop();
        delete application;
    } catch (FIX::Exception& e) { std::cout << e.what() << std::endl; }
    return 0;
}