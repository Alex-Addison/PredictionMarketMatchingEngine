#include <quickfix/Application.h>
#include <quickfix/Values.h>
#include <quickfix/ThreadedSocketAcceptor.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Session.h>

#include <iostream>
#include <map>
#include <queue>
#include <mutex>
#include <string>

// Order structure for LOB
struct Order {
    std::string clOrdID;
    std::string symbol;
    double price;
    int quantity;
    char side;
};

class MatchingEngine : public FIX::Application {
private:
    std::string engineName;
    std::mutex bookMutex;
    
    // Nested Maps: Symbol -> Price -> Order Queue (price-time priority)
    // TODO: optimize data structure for better performance (e.g. use balanced tree for price levels)
    // TODO: multithreading support for concurrent order processing and matching
    std::map<std::string, std::map<double, std::queue<Order>>> bids;
    std::map<std::string, std::map<double, std::queue<Order>>> asks;

public:
    // Override FIX's pure virtual functions
    void onCreate(const FIX::SessionID&) override {}
    void onLogout(const FIX::SessionID&) override {}
    void toAdmin(FIX::Message&, const FIX::SessionID&) override {}
    void toApp(FIX::Message&, const FIX::SessionID&) throw(FIX::DoNotSend) override {}
    void fromAdmin(const FIX::Message&, const FIX::SessionID&) throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) override {}


    void onLogon(const FIX::SessionID& sessionID) override {
        engineName = sessionID.getSenderCompID().getString();
        std::cout << "[" << engineName << "] Logon successful." << std::endl;
    }

    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) 
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override {
        // Only process NewOrderSingle messages for simplicity, ignore others
        // TODO: add support for order cancellations, modifications, etc.
        if (message.getHeader().getField(FIX::FIELD::MsgType) == FIX::MsgType_NewOrderSingle) {
            processOrder(message, sessionID);
        }
    }

    void processOrder(const FIX::Message& msg, const FIX::SessionID& sessionID) {
        // Lock the book while processing to ensure thread safety
        std::lock_guard<std::mutex> lock(bookMutex);
        // Extract order details from the FIX message
        std::string symbol = msg.getField(FIX::FIELD::Symbol);
        char side = msg.getField(FIX::FIELD::Side)[0];
        std::string priceStr = msg.getField(FIX::FIELD::Price);
        std::string qtyStr = msg.getField(FIX::FIELD::OrderQty);
        std::string clOrdID = msg.getField(FIX::FIELD::ClOrdID);

        // Data Storage (Pushing into LOB)
        Order o = {clOrdID, symbol, std::stod(priceStr), std::stoi(qtyStr), side};
        // buy yes / sell no
        if (side == '1') bids[symbol][o.price].push(o);
        // buy no / sell yes
        else asks[symbol][o.price].push(o);

        std::cout << "[" << engineName << "] Booked: " << symbol << " | Side: " << side << " | Px: " << priceStr << std::endl;

        // generate report back to gateway
        FIX::Message report;
        report.getHeader().setField(FIX::BeginString("FIXT.1.1"));
        report.getHeader().setField(FIX::MsgType(FIX::MsgType_ExecutionReport));
        report.setField(FIX::OrderID("MKT_" + clOrdID));
        report.setField(FIX::ExecID("EXEC_" + clOrdID));
        report.setField(FIX::ExecType(FIX::ExecType_NEW));
        report.setField(FIX::OrdStatus(FIX::OrdStatus_NEW));
        report.setField(FIX::Symbol(symbol));
        report.setField(FIX::Side(side));
        report.setField(FIX::LeavesQty(o.quantity));
        report.setField(FIX::CumQty(0));
        report.setField(FIX::OrderQty(o.quantity));
        report.setField(FIX::ClOrdID(clOrdID));
        report.setField(FIX::TransactTime());
        // TODO: change to actual execution price after matching logic is implemented
        report.setField(FIX::AvgPx(0.0));
        
        FIX::Session::sendToTarget(report, sessionID);
    }
};

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        FIX::SessionSettings settings(argv[1]);
        MatchingEngine application;
        FIX::FileStoreFactory storeFactory(settings);
        FIX::FileLogFactory logFactory(settings);
        FIX::ThreadedSocketAcceptor acceptor(application, storeFactory, settings, logFactory);
        acceptor.start();
        std::cin.get();
        acceptor.stop();
        return 0;
    } catch (FIX::Exception& e) {
        std::cout << e.what() << std::endl;
        return 1;
    }
}