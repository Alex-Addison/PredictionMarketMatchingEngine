#include <quickfix/Application.h>
#include <quickfix/Values.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Session.h>

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

class TraderApp : public FIX::Application {
public:
    // Override FIX's pure virtual functions
    void onCreate(const FIX::SessionID&) override {}
    void onLogout(const FIX::SessionID&) override {}
    void toAdmin(FIX::Message&, const FIX::SessionID&) override {}
    void toApp(FIX::Message&, const FIX::SessionID&) throw(FIX::DoNotSend) override {}
    void fromAdmin(const FIX::Message&, const FIX::SessionID&) throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) override {}

    // send test buy orders of qty 100 at price 0.55
    void sendOrder(const FIX::SessionID& sessionID, std::string id, std::string symbol) {
        // create order message
        FIX::Message order;
        order.getHeader().setField(FIX::BeginString("FIXT.1.1"));
        order.getHeader().setField(FIX::MsgType(FIX::MsgType_NewOrderSingle));
        order.setField(FIX::ClOrdID(id));
        order.setField(FIX::Symbol(symbol));
        order.setField(FIX::Side(FIX::Side_BUY)); 
        order.setField(FIX::OrderQty(100));
        order.setField(FIX::Price(0.55));
        order.setField(FIX::OrdType(FIX::OrdType_LIMIT));
        //order.setField(FIX::TransactTime());
        
        FIX::Session::sendToTarget(order, sessionID);
        std::cout << "[TRADER] Sent order for: " << symbol << std::endl;
    }

    // on logon, send order for 2 seperate symbols to test routing logic in gateway
    void onLogon(const FIX::SessionID& sessionID) override {
        std::cout << "[TRADER] Logged into Gateway. Sending orders..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 3 test inputs
        sendOrder(sessionID, "TRD_001", "KXFED-26DEC-5.00");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        sendOrder(sessionID, "TRD_002", "KXWEATHER-MIA-85");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        sendOrder(sessionID, "TRD_003", "KXWEATHER-NYC-30");
    }

    // print execution reports received from gateway
    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) 
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override {
        
        if (message.getHeader().getField(FIX::FIELD::MsgType) == FIX::MsgType_ExecutionReport) {
            std::cout << "[TRADER] ExecReport Received for: " 
                      << message.getField(FIX::FIELD::Symbol) << std::endl;
        }
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
        std::cin.get();
        initiator.stop();
        return 0;
    } catch (FIX::Exception& e) {
        std::cout << e.what() << std::endl;
        return 1;
    }
}