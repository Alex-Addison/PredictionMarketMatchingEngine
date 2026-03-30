#include <quickfix/Application.h>
#include <quickfix/Values.h>
#include <quickfix/ThreadedSocketAcceptor.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Session.h>
#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <sstream>

class GatewayApp : public FIX::Application {
private:
    // Routing table from ticker to matching engine
    std::map<std::string, std::string> routingMap;          
    // Map of engineID to SessionID for active connections
    std::map<std::string, FIX::SessionID> engineSessions;
    // SessionID for the trader connection
    FIX::SessionID traderSessionID;
    //TODO: add support for multiple traders

    // Load routing table from routing.txt file
    void loadRoutingTable() {
        std::ifstream file("routing.txt");
        std::string line;
        // iterate over entries in routing.txt and populate routingMap
        while (std::getline(file, line)) {
            // find the position of the '=' delimiter
            auto delimPos = line.find('=');
            // split the line into prefix and engineID, then add to map
            if (delimPos != std::string::npos) {
                std::string prefix = line.substr(0, delimPos);
                std::string engineID = line.substr(delimPos + 1);
                routingMap[prefix] = engineID;
                std::cout << "[GATEWAY] Loaded Route: " << prefix << " -> " << engineID << std::endl;
            }
            // routing.txt format error handling
            else {
                std::cout << "[GATEWAY ERROR] Invalid routing entry: " << line << std::endl;
            }
        }
    }

public:
    //constructor, load routing table on startup
    GatewayApp() { loadRoutingTable(); }

    // ovverride FIX's pure virtual functions
    void onCreate(const FIX::SessionID&) override {}
    void onLogout(const FIX::SessionID&) override {}
    void toAdmin(FIX::Message&, const FIX::SessionID&) override {}
    void toApp(FIX::Message&, const FIX::SessionID&) throw(FIX::DoNotSend) override {}
    void fromAdmin(const FIX::Message&, const FIX::SessionID&) throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) override {}

    // Handle new logons for trader/engine connections
    void onLogon(const FIX::SessionID& sessionID) override {
        std::string target = sessionID.getTargetCompID().getString();
        if (target == "TRADER") {
            traderSessionID = sessionID;
            std::cout << "[GATEWAY] Trader connected." << std::endl;
        } else {
            // Other connections are assumed to be an Engine
            engineSessions[target] = sessionID;
            std::cout << "[GATEWAY] Connected to Matching Engine: " << target << std::endl;
        }
    }

    // Handle incoming messages from traders and engines
    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) 
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override {
        
        // get sender from sessionID
        std::string sender = sessionID.getTargetCompID().getString();
        
        // mutable copy of message to modify and forward
        FIX::Message forwardMessage = message;
        
        if (sender == "TRADER") {
            // extract symbol from message
            std::string symbol = message.getField(FIX::FIELD::Symbol);
            std::string targetEngine = "";

            // Find engine based on symbol prefix (market identifier)
            for (const auto& pair : routingMap) {
                if (symbol.find(pair.first) != std::string::npos) {
                    targetEngine = pair.second;
                    break;
                }
            }

            // Route the message to the appropriate engine if found
            if (!targetEngine.empty() && engineSessions.count(targetEngine)) {
                std::cout << "[GATEWAY] Routing " << symbol << " -> " << targetEngine << std::endl;
                FIX::Session::sendToTarget(forwardMessage, engineSessions[targetEngine]);
            } else {
                std::cout << "[GATEWAY ERROR] No route for symbol: " << symbol << std::endl;
            }
        } 
        else {
            // Message came from an Engine, route the Execution Report back to the Trader
            std::cout << "[GATEWAY] Routing ExecReport from " << sender << " -> TRADER" << std::endl;
            FIX::Session::sendToTarget(forwardMessage, traderSessionID);
        }
    }
};

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        // set our settings (from config file)
        FIX::SessionSettings settings(argv[1]);
        // create our application
        GatewayApp application;
        // set store and logs get put in the store/ and store/log/ directories
        FIX::FileStoreFactory storeFactory(settings);
        FIX::FileLogFactory logFactory(settings);
        // create acceptor for incoming connections and initiator for outgoing connections
        FIX::ThreadedSocketAcceptor acceptor(application, storeFactory, settings, logFactory);
        FIX::SocketInitiator initiator(application, storeFactory, settings, logFactory);
        // start both acceptor and initiator
        acceptor.start();
        initiator.start();

        // block until user hits enter
        std::cin.get();

        // close both acceptor and initiator on shutdown
        initiator.stop();
        acceptor.stop();
        return 0;
    } catch (FIX::Exception& e) {
        std::cout << e.what() << std::endl;
        return 1;
    }
}